## CoAP implementation

CoAP core specification
https://tools.ietf.org/html/rfc7252

CoAP block specification
https://tools.ietf.org/html/rfc7959


# Clarifications

### How long can it be from a request is made until a response is ready.

A coap exchange has a limited timespan, but a coap request/response
can take as long as the connection is alive.

### Why are there no tokens in the ACK and RST messages

It states here that the correlation is alone based on the messageId,
in another place it's noted that an ack to a con is a 4 byte packet
with the empty code.  https://tools.ietf.org/html/rfc7252#section-4.4

The reason for this is that each CON transmission is identified alone
by the message id and endpoint.


### Piggybacked vs separate responses.

If we are using piggybacked responses, i.e. sending the response in an
ack packet. We need to keep the response state until we are sure that
the client will not ask the same request again. If we instead always
use separate responses the client is forced to send an ack or
equivalent to acknowledge the receive of the transaction. In this case
we shall only keep the response state until the ack is received.


## Route Tree

The resources in a coap server is represented as a route tree

/
/iam
/iam/users -> Get handler
/iam/users -> Post handler
/iam/users/{user} -> Get Handler
/iam/users/{user} -> Delete handler
/iam/users/{user}/roles/{role} -> Put Handler
/iam/users/{user}/roles/{role} -> Delete Handler
/iam/roles -> Get handler
/iam/roles -> Post handler
/iam/roles/{role} -> Get handler
/iam/roles/{role} -> Delete handler
/iam/roles/{role}/policies/{policy} -> Put handler
/iam/roles/{role}/policies/{policy} -> Delete handler

struct PathSegment {
    const char* name;
    struct RouteNode* node;

    struct PathSegment* next;
    struct PathSegment* prev;
};



struct ParameterSegment {
    const char* parameterName;
    struct RouteNode* node;
};

struct RouteNode {
    PathSegment sentinel;
    struct Parameter* parameter;
    ResourceHandler GetHandler;
    ResourceHandler PostHandler;
    ResourceHandler PutHandler;
    ResourceHandler DeleteHandler;
};
