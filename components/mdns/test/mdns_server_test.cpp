#include <boost/test/unit_test.hpp>
#include <nabto_mdns/nabto_mdns_server.h>

#include <nn/string_map.h>
#include <nn/string_set.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct nn_allocator defaultAllocator = {
    &calloc,
    &free
};

const uint16_t MDNS_CLASS_IN = 1;
const uint16_t MDNS_CACHE_FLUSH = (1 << 15);
const uint16_t MDNS_FLAG_RESPONSE = (1 << 15);
const uint16_t MDNS_FLAG_AUTHORITATIVE = (1 << 10);
const size_t MDNS_HEADER_SIZE = 12;

/**
 * A server configured with a fixed instance name, one subtype and one
 * txt item. The nn containers must outlive the server context.
 */
class TestServer {
 public:
    TestServer(const char* instanceName = "myinstance")
    {
        nn_string_set_init(&subtypes_, &defaultAllocator);
        nn_string_map_init(&txtItems_, &defaultAllocator);
        nn_string_set_insert(&subtypes_, "heatpump");
        nn_string_map_insert(&txtItems_, "productid", "pr-12345678");
        nn_string_map_insert(&txtItems_, "deviceid", "de-abcdefgh");
        nabto_mdns_server_init(&ctx);
        nabto_mdns_server_update_info(&ctx, instanceName, &subtypes_, &txtItems_);
    }
    ~TestServer()
    {
        nn_string_map_deinit(&txtItems_);
        nn_string_set_deinit(&subtypes_);
    }

    size_t subtypeCount() { return nn_string_set_size(&subtypes_); }

    struct nabto_mdns_server_context ctx;

 private:
    struct nn_string_set subtypes_;
    struct nn_string_map txtItems_;
};

/**
 * Builds an mDNS query packet.
 */
class QueryBuilder {
 public:
    QueryBuilder(uint16_t id, uint16_t flags = 0)
    {
        put16(id);
        put16(flags);
        put16(0); // qdcount, patched by question()
        put16(0); // ancount
        put16(0); // nscount
        put16(0); // arcount
    }

    // Append a question with a fully written out name.
    QueryBuilder& question(std::vector<const char*> labels, uint16_t type = NABTO_MDNS_PTR)
    {
        for (const char* l : labels) {
            label(l);
        }
        put8(0);
        finishQuestion(type);
        return *this;
    }

    // Append a question whose name ends in a compression pointer to `offset`.
    QueryBuilder& compressedQuestion(std::vector<const char*> labels, uint16_t offset, uint16_t type = NABTO_MDNS_PTR)
    {
        for (const char* l : labels) {
            label(l);
        }
        put16(0xC000 | offset);
        finishQuestion(type);
        return *this;
    }

    size_t size() const { return buf_.size(); }
    const uint8_t* data() const { return buf_.data(); }

    // Offset the next byte will be written at.
    uint16_t offset() const { return (uint16_t)buf_.size(); }

 private:
    void put8(uint8_t v) { buf_.push_back(v); }
    void put16(uint16_t v) { put8((uint8_t)(v >> 8)); put8((uint8_t)(v & 0xff)); }
    void label(const char* l)
    {
        size_t n = strlen(l);
        put8((uint8_t)n);
        buf_.insert(buf_.end(), l, l + n);
    }
    void finishQuestion(uint16_t type)
    {
        put16(type);
        put16(MDNS_CLASS_IN);
        questions_++;
        buf_[4] = (uint8_t)(questions_ >> 8);
        buf_[5] = (uint8_t)(questions_ & 0xff);
    }

    std::vector<uint8_t> buf_;
    uint16_t questions_ = 0;
};

bool handle(TestServer& s, const QueryBuilder& q, uint16_t* id)
{
    return nabto_mdns_server_handle_packet(&s.ctx, q.data(), q.size(), id);
}

/**
 * Minimal DNS response parser used to verify the packets the server builds.
 */
struct ResourceRecord {
    std::string name;
    uint16_t type;
    uint16_t cls;
    uint32_t ttl;
    size_t rdataOffset;
    uint16_t rdlength;
};

class ResponseParser {
 public:
    ResponseParser(const uint8_t* buf, size_t size) : buf_(buf, buf + size) {}

    uint16_t u16(size_t off) const
    {
        BOOST_REQUIRE(off + 2 <= buf_.size());
        return (uint16_t)((buf_[off] << 8) | buf_[off + 1]);
    }
    uint32_t u32(size_t off) const
    {
        return ((uint32_t)u16(off) << 16) | u16(off + 2);
    }

    uint16_t id() const { return u16(0); }
    uint16_t flags() const { return u16(2); }
    uint16_t qdcount() const { return u16(4); }
    uint16_t ancount() const { return u16(6); }
    uint16_t nscount() const { return u16(8); }
    uint16_t arcount() const { return u16(10); }

    // Decode a possibly compressed name starting at `off` into dotted form.
    // Returns the offset just after the name as it appears in the packet.
    size_t readName(size_t off, std::string& out) const
    {
        out.clear();
        size_t next = 0;
        int hops = 0;
        for (;;) {
            BOOST_REQUIRE(off < buf_.size());
            uint8_t len = buf_[off];
            if ((len & 0xC0) == 0xC0) {
                BOOST_REQUIRE(++hops < 16); // guard against pointer loops
                uint16_t ptr = u16(off) & 0x3FFF;
                BOOST_REQUIRE(ptr < off);
                if (next == 0) {
                    next = off + 2;
                }
                off = ptr;
                continue;
            }
            off++;
            if (len == 0) {
                break;
            }
            BOOST_REQUIRE(off + len <= buf_.size());
            if (!out.empty()) {
                out += ".";
            }
            out.append((const char*)&buf_[off], len);
            off += len;
        }
        return next == 0 ? off : next;
    }

    std::vector<ResourceRecord> answers() const
    {
        std::vector<ResourceRecord> out;
        size_t off = MDNS_HEADER_SIZE;
        BOOST_REQUIRE_EQUAL(qdcount(), 0);
        for (uint16_t i = 0; i < ancount(); i++) {
            ResourceRecord rr;
            off = readName(off, rr.name);
            rr.type = u16(off);
            rr.cls = u16(off + 2);
            rr.ttl = u32(off + 4);
            rr.rdlength = u16(off + 8);
            rr.rdataOffset = off + 10;
            off = rr.rdataOffset + rr.rdlength;
            BOOST_REQUIRE(off <= buf_.size());
            out.push_back(rr);
        }
        BOOST_CHECK_EQUAL(off, buf_.size()); // no trailing garbage
        return out;
    }

    const ResourceRecord* find(const std::vector<ResourceRecord>& rrs, uint16_t type, const std::string& name) const
    {
        for (const ResourceRecord& rr : rrs) {
            if (rr.type == type && rr.name == name) {
                return &rr;
            }
        }
        return nullptr;
    }

    // rdata helpers
    std::string ptrTarget(const ResourceRecord& rr) const
    {
        std::string s;
        size_t end = readName(rr.rdataOffset, s);
        BOOST_CHECK_EQUAL(end, rr.rdataOffset + rr.rdlength);
        return s;
    }
    uint16_t srvPort(const ResourceRecord& rr) const { return u16(rr.rdataOffset + 4); }
    std::string srvTarget(const ResourceRecord& rr) const
    {
        std::string s;
        size_t end = readName(rr.rdataOffset + 6, s);
        BOOST_CHECK_EQUAL(end, rr.rdataOffset + rr.rdlength);
        return s;
    }
    std::vector<std::string> txtStrings(const ResourceRecord& rr) const
    {
        std::vector<std::string> out;
        size_t off = rr.rdataOffset;
        size_t end = rr.rdataOffset + rr.rdlength;
        while (off < end) {
            uint8_t len = buf_[off++];
            BOOST_REQUIRE(off + len <= end);
            out.push_back(std::string((const char*)&buf_[off], len));
            off += len;
        }
        return out;
    }
    std::vector<uint8_t> rdata(const ResourceRecord& rr) const
    {
        return std::vector<uint8_t>(buf_.begin() + rr.rdataOffset, buf_.begin() + rr.rdataOffset + rr.rdlength);
    }

 private:
    std::vector<uint8_t> buf_;
};

struct nn_ip_address v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    struct nn_ip_address ip;
    ip.type = NN_IPV4;
    uint8_t bytes[4] = { a, b, c, d };
    memcpy(ip.ip.v4, bytes, 4);
    return ip;
}

struct nn_ip_address v6LinkLocal()
{
    struct nn_ip_address ip;
    ip.type = NN_IPV6;
    memset(ip.ip.v6, 0, 16);
    ip.ip.v6[0] = 0xfe; ip.ip.v6[1] = 0x80; ip.ip.v6[15] = 0x01;
    return ip;
}

} // namespace

BOOST_AUTO_TEST_SUITE(mdns)

// nabto_mdns_server_handle_packet

BOOST_AUTO_TEST_CASE(query_ignored_when_server_has_no_info)
{
    struct nabto_mdns_server_context ctx;
    nabto_mdns_server_init(&ctx);

    QueryBuilder q(1);
    q.question({ "_nabto", "_udp", "local" });
    uint16_t id = 0;
    BOOST_TEST(!nabto_mdns_server_handle_packet(&ctx, q.data(), q.size(), &id));
}

BOOST_AUTO_TEST_CASE(service_ptr_query_is_answered)
{
    TestServer s;
    QueryBuilder q(0x1234);
    q.question({ "_nabto", "_udp", "local" });
    uint16_t id = 0;
    BOOST_TEST(handle(s, q, &id));
    BOOST_TEST(id == 0x1234);
}

BOOST_AUTO_TEST_CASE(instance_query_is_answered)
{
    TestServer s;
    QueryBuilder q(2);
    q.question({ "myinstance", "_nabto", "_udp", "local" }, NABTO_MDNS_SRV);
    uint16_t id = 0;
    BOOST_TEST(handle(s, q, &id));
}

BOOST_AUTO_TEST_CASE(hostname_query_is_answered)
{
    TestServer s;
    QueryBuilder q(3);
    q.question({ "myinstance", "local" }, NABTO_MDNS_A);
    uint16_t id = 0;
    BOOST_TEST(handle(s, q, &id));
}

BOOST_AUTO_TEST_CASE(service_enumeration_query_is_answered)
{
    TestServer s;
    QueryBuilder q(4);
    q.question({ "_services", "_dns-sd", "_udp", "local" });
    uint16_t id = 0;
    BOOST_TEST(handle(s, q, &id));
}

BOOST_AUTO_TEST_CASE(subtype_query_is_answered_only_for_registered_subtypes)
{
    TestServer s;
    uint16_t id = 0;

    QueryBuilder known(5);
    known.question({ "heatpump", "_sub", "_nabto", "_udp", "local" });
    BOOST_TEST(handle(s, known, &id));

    QueryBuilder unknown(6);
    unknown.question({ "thermostat", "_sub", "_nabto", "_udp", "local" });
    BOOST_TEST(!handle(s, unknown, &id));
}

BOOST_AUTO_TEST_CASE(unrelated_query_is_ignored)
{
    TestServer s;
    QueryBuilder q(7);
    q.question({ "_http", "_tcp", "local" });
    uint16_t id = 0;
    BOOST_TEST(!handle(s, q, &id));
}

BOOST_AUTO_TEST_CASE(other_instance_query_is_ignored)
{
    TestServer s;
    QueryBuilder q(8);
    q.question({ "otherinstance", "_nabto", "_udp", "local" }, NABTO_MDNS_SRV);
    uint16_t id = 0;
    BOOST_TEST(!handle(s, q, &id));
}

BOOST_AUTO_TEST_CASE(response_packets_are_ignored)
{
    TestServer s;
    QueryBuilder q(9, MDNS_FLAG_RESPONSE | MDNS_FLAG_AUTHORITATIVE);
    q.question({ "_nabto", "_udp", "local" });
    uint16_t id = 0;
    BOOST_TEST(!handle(s, q, &id));
}

BOOST_AUTO_TEST_CASE(name_matching_is_case_insensitive)
{
    TestServer s;
    QueryBuilder q(10);
    q.question({ "MyInstance", "_NABTO", "_Udp", "LOCAL" }, NABTO_MDNS_TXT);
    uint16_t id = 0;
    BOOST_TEST(handle(s, q, &id));
}

BOOST_AUTO_TEST_CASE(later_question_in_packet_is_matched)
{
    TestServer s;
    QueryBuilder q(11);
    q.question({ "_http", "_tcp", "local" });
    q.question({ "_ipp", "_tcp", "local" });
    q.question({ "_nabto", "_udp", "local" });
    uint16_t id = 0;
    BOOST_TEST(handle(s, q, &id));
}

BOOST_AUTO_TEST_CASE(compressed_name_is_matched)
{
    TestServer s;
    QueryBuilder q(12);
    // First question is unrelated but contains "_udp.local" which the
    // second question references through a compression pointer.
    uint16_t udpOffset = q.offset() + 1 + strlen("foo") + 1 + strlen("_nabto");
    q.question({ "foo", "_nabto", "_udp", "local" });
    q.compressedQuestion({ "_nabto" }, udpOffset);
    uint16_t id = 0;
    BOOST_TEST(handle(s, q, &id));
}

BOOST_AUTO_TEST_CASE(forward_compression_pointer_is_rejected)
{
    TestServer s;
    QueryBuilder q(13);
    // compression pointers must point to earlier data in the packet
    q.compressedQuestion({}, MDNS_HEADER_SIZE + 10);
    uint16_t id = 0;
    BOOST_TEST(!handle(s, q, &id));
}

BOOST_AUTO_TEST_CASE(truncated_packets_are_rejected)
{
    TestServer s;
    uint16_t id = 0;

    QueryBuilder q(14);
    q.question({ "_nabto", "_udp", "local" });

    // header only
    BOOST_TEST(!nabto_mdns_server_handle_packet(&s.ctx, q.data(), MDNS_HEADER_SIZE, &id));
    // cut inside the name
    BOOST_TEST(!nabto_mdns_server_handle_packet(&s.ctx, q.data(), MDNS_HEADER_SIZE + 4, &id));
    // shorter than the header
    BOOST_TEST(!nabto_mdns_server_handle_packet(&s.ctx, q.data(), 3, &id));
    BOOST_TEST(!nabto_mdns_server_handle_packet(&s.ctx, q.data(), 0, &id));
}

// nabto_mdns_server_build_packet

BOOST_AUTO_TEST_CASE(build_packet_fails_when_server_has_no_info)
{
    struct nabto_mdns_server_context ctx;
    nabto_mdns_server_init(&ctx);
    struct nn_ip_address ip = v4(10, 0, 0, 1);
    uint8_t buf[1500];
    size_t written = 0;
    BOOST_TEST(!nabto_mdns_server_build_packet(&ctx, 1, false, false, &ip, 1, 4242, buf, sizeof(buf), &written));
}

BOOST_AUTO_TEST_CASE(build_packet_header)
{
    TestServer s;
    struct nn_ip_address ips[2] = { v4(192, 168, 1, 10), v6LinkLocal() };
    uint8_t buf[1500];
    size_t written = 0;
    BOOST_REQUIRE(nabto_mdns_server_build_packet(&s.ctx, 0xBEEF, false, false, ips, 2, 4242, buf, sizeof(buf), &written));
    BOOST_TEST(written > MDNS_HEADER_SIZE);

    ResponseParser p(buf, written);
    BOOST_TEST(p.id() == 0xBEEF);
    BOOST_TEST(p.flags() == (MDNS_FLAG_RESPONSE | MDNS_FLAG_AUTHORITATIVE));
    BOOST_TEST(p.qdcount() == 0);
    // PTR service, PTR dns-sd, SRV, TXT, one per ip, one per subtype
    BOOST_TEST(p.ancount() == 4 + 2 + s.subtypeCount());
    BOOST_TEST(p.nscount() == 0);
    BOOST_TEST(p.arcount() == 0);
    BOOST_TEST(p.answers().size() == p.ancount());
}

BOOST_AUTO_TEST_CASE(build_packet_records)
{
    TestServer s;
    struct nn_ip_address ips[2] = { v4(192, 168, 1, 10), v6LinkLocal() };
    uint8_t buf[1500];
    size_t written = 0;
    BOOST_REQUIRE(nabto_mdns_server_build_packet(&s.ctx, 1, false, false, ips, 2, 4242, buf, sizeof(buf), &written));

    ResponseParser p(buf, written);
    std::vector<ResourceRecord> rrs = p.answers();

    for (const ResourceRecord& rr : rrs) {
        BOOST_TEST(rr.ttl == 120u);
    }

    const ResourceRecord* service = p.find(rrs, NABTO_MDNS_PTR, "_nabto._udp.local");
    BOOST_REQUIRE(service != nullptr);
    BOOST_TEST(service->cls == MDNS_CLASS_IN);
    BOOST_TEST(p.ptrTarget(*service) == "myinstance._nabto._udp.local");

    const ResourceRecord* subtype = p.find(rrs, NABTO_MDNS_PTR, "heatpump._sub._nabto._udp.local");
    BOOST_REQUIRE(subtype != nullptr);
    BOOST_TEST(p.ptrTarget(*subtype) == "myinstance._nabto._udp.local");

    const ResourceRecord* dnssd = p.find(rrs, NABTO_MDNS_PTR, "_services._dns-sd._udp.local");
    BOOST_REQUIRE(dnssd != nullptr);
    BOOST_TEST(p.ptrTarget(*dnssd) == "_nabto._udp.local");

    const ResourceRecord* srv = p.find(rrs, NABTO_MDNS_SRV, "myinstance._nabto._udp.local");
    BOOST_REQUIRE(srv != nullptr);
    BOOST_TEST(srv->cls == (MDNS_CLASS_IN | MDNS_CACHE_FLUSH));
    BOOST_TEST(p.srvPort(*srv) == 4242);
    BOOST_TEST(p.srvTarget(*srv) == "myinstance.local");

    const ResourceRecord* txt = p.find(rrs, NABTO_MDNS_TXT, "myinstance._nabto._udp.local");
    BOOST_REQUIRE(txt != nullptr);
    BOOST_TEST(txt->cls == (MDNS_CLASS_IN | MDNS_CACHE_FLUSH));
    std::vector<std::string> txts = p.txtStrings(*txt);
    BOOST_TEST(txts.size() == 2u);
    BOOST_TEST((std::find(txts.begin(), txts.end(), "productid=pr-12345678") != txts.end()));
    BOOST_TEST((std::find(txts.begin(), txts.end(), "deviceid=de-abcdefgh") != txts.end()));

    const ResourceRecord* a = p.find(rrs, NABTO_MDNS_A, "myinstance.local");
    BOOST_REQUIRE(a != nullptr);
    BOOST_TEST(a->cls == (MDNS_CLASS_IN | MDNS_CACHE_FLUSH));
    std::vector<uint8_t> expectedV4 = { 192, 168, 1, 10 };
    BOOST_TEST(p.rdata(*a) == expectedV4);

    const ResourceRecord* aaaa = p.find(rrs, NABTO_MDNS_AAAA, "myinstance.local");
    BOOST_REQUIRE(aaaa != nullptr);
    std::vector<uint8_t> expectedV6(ips[1].ip.v6, ips[1].ip.v6 + 16);
    BOOST_TEST(p.rdata(*aaaa) == expectedV6);
}

BOOST_AUTO_TEST_CASE(build_packet_without_ips_has_no_address_records)
{
    TestServer s;
    uint8_t buf[1500];
    size_t written = 0;
    BOOST_REQUIRE(nabto_mdns_server_build_packet(&s.ctx, 1, false, false, NULL, 0, 4242, buf, sizeof(buf), &written));

    ResponseParser p(buf, written);
    std::vector<ResourceRecord> rrs = p.answers();
    BOOST_TEST(rrs.size() == 4 + s.subtypeCount());
    BOOST_TEST(p.find(rrs, NABTO_MDNS_A, "myinstance.local") == nullptr);
    BOOST_TEST(p.find(rrs, NABTO_MDNS_AAAA, "myinstance.local") == nullptr);
}

BOOST_AUTO_TEST_CASE(goodbye_packet_has_zero_ttl)
{
    TestServer s;
    struct nn_ip_address ip = v4(10, 0, 0, 1);
    uint8_t buf[1500];
    size_t written = 0;
    BOOST_REQUIRE(nabto_mdns_server_build_packet(&s.ctx, 1, false, true, &ip, 1, 4242, buf, sizeof(buf), &written));

    ResponseParser p(buf, written);
    std::vector<ResourceRecord> rrs = p.answers();
    BOOST_TEST(!rrs.empty());
    for (const ResourceRecord& rr : rrs) {
        BOOST_TEST(rr.ttl == 0u);
    }
}

BOOST_AUTO_TEST_CASE(unicast_response_does_not_set_cache_flush)
{
    TestServer s;
    struct nn_ip_address ip = v4(10, 0, 0, 1);
    uint8_t buf[1500];
    size_t written = 0;
    BOOST_REQUIRE(nabto_mdns_server_build_packet(&s.ctx, 1, true, false, &ip, 1, 4242, buf, sizeof(buf), &written));

    ResponseParser p(buf, written);
    for (const ResourceRecord& rr : p.answers()) {
        BOOST_TEST_CONTEXT("record " << rr.name << " type " << rr.type) {
            BOOST_TEST(rr.cls == MDNS_CLASS_IN);
        }
    }
}

BOOST_AUTO_TEST_CASE(build_packet_fails_when_buffer_is_too_small)
{
    TestServer s;
    struct nn_ip_address ips[2] = { v4(192, 168, 1, 10), v6LinkLocal() };
    uint8_t buf[64];
    size_t written = 0;
    BOOST_TEST(!nabto_mdns_server_build_packet(&s.ctx, 1, false, false, ips, 2, 4242, buf, sizeof(buf), &written));
    BOOST_TEST(!nabto_mdns_server_build_packet(&s.ctx, 1, false, false, ips, 2, 4242, buf, 0, &written));
}

BOOST_AUTO_TEST_CASE(build_packet_fails_for_every_size_below_the_packet_size)
{
    TestServer s;
    struct nn_ip_address ips[2] = { v4(192, 168, 1, 10), v6LinkLocal() };
    uint8_t full[1500];
    size_t fullSize = 0;
    BOOST_REQUIRE(nabto_mdns_server_build_packet(&s.ctx, 1, false, false, ips, 2, 4242, full, sizeof(full), &fullSize));

    // Undersized buffers must be rejected without writing past them,
    // an exactly sized buffer must produce the same packet.
    std::vector<uint8_t> buf(fullSize);
    for (size_t size = 0; size < fullSize; size++) {
        size_t written = 0;
        BOOST_TEST_CONTEXT("buffer size " << size) {
            BOOST_TEST(!nabto_mdns_server_build_packet(&s.ctx, 1, false, false, ips, 2, 4242, buf.data(), size, &written));
        }
    }
    size_t written = 0;
    BOOST_TEST(nabto_mdns_server_build_packet(&s.ctx, 1, false, false, ips, 2, 4242, buf.data(), fullSize, &written));
    BOOST_TEST(written == fullSize);
    BOOST_TEST(std::equal(buf.begin(), buf.end(), full));
}

BOOST_AUTO_TEST_CASE(built_response_is_not_handled_as_a_query)
{
    TestServer s;
    struct nn_ip_address ip = v4(10, 0, 0, 1);
    uint8_t buf[1500];
    size_t written = 0;
    BOOST_REQUIRE(nabto_mdns_server_build_packet(&s.ctx, 1, false, false, &ip, 1, 4242, buf, sizeof(buf), &written));

    uint16_t id = 0;
    BOOST_TEST(!nabto_mdns_server_handle_packet(&s.ctx, buf, written, &id));
}

BOOST_AUTO_TEST_SUITE_END()
