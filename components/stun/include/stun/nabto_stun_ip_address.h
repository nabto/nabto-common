#ifndef NABTO_STUN_IP_ADDRESS_H
#define NABTO_STUN_IP_ADDRESS_H

enum nabto_stun_ip_address_type {
    NABTO_STUN_IPV4,
    NABTO_STUN_IPV6
};


// network order ip address
struct nabto_stun_ipv4_address {
    uint8_t addr[4];
};

// network order ipv6 address
struct nabto_stun_ipv6_address {
    uint8_t addr[16];
};

struct nabto_stun_ip_address {
    enum nabto_stun_ip_address_type type;
    union {
        struct nabto_stun_ipv4_address v4;
        struct nabto_stun_ipv6_address v6;
    };
};

struct nabto_stun_endpoint {
    struct nabto_stun_ip_address addr;
    uint16_t port;
};
#endif // NABTO_STUN_IP_ADDRESS_H
