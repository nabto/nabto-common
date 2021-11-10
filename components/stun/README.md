# Stun client


## Tests

This code has a test suite which is located in the private
nabto-common-cpp repository.




# Full stun test protocol

The stun client runs 7 tests as shown below.

 * Client is the stun client
 * SXPY   is Server IP X and server Port Y (1: Primary, 2: Secondary)
 * TX     is the label of each test
 * SockX  is which socket the test is using (1: Primary, 2: Secondary)
 * f f    means the change Port and change address bits are false. (t means the bits are true)

```
       Client       S1P1       S1P2       S2P1      S2P2
T1       |   f f     |          |          |          |
Sock1    |---------> |          |          |          |
         |<--------  |          |          |          |
         |           |          |          |          |
T2       |   f f     |          |          |          |
Sock1    |--------------------->|          |          |
         |<-------------------- |          |          |
         |           |          |          |          |
T3       |   f f     |          |          |          |
Sock1    |-------------------------------> |          |
         |<------------------------------  |          |
         |           |          |          |          |
T4       |   t f     |          |          |          |
Sock2    |---------> |          |          |          |
         |<-------------------- |          |          |
         |           |          |          |          |
T5       |   f t     |          |          |          |
Sock2    |---------> |          |          |          |
         |<------------------------------- |          |
         |           |          |          |          |
T6       |   f f     |          |          |          |
Sock2    |---------> |          |          |          |
         |<--------- |          |          |          |
         |           |          |          |          |
T7       |   f f     |          |          |          |
Sock2    |-------------------------------> |          |
         |<------------------------------- |          |
```

The stun client conducts tests in 3 stages: `INITIAL_TEST, TESTING, DEFECT_ROUTER_TEST`.

In the `INITIAL_TEST` stage, the client send the T1 test to all provided stun server endpoints (likely all ip assosiated with a given DNS). The first stun server to respond will be used for the rest of the tests.

In the `TESTING` stage, T2-T6 are sendt in parrallel.

Finally in stage `DEFECT_ROUTER_TEST`, will run `T7`

For each test, the resulting mapped endpoint is recorded, here we denote the resulting mapped port for test T1: `PM-T1`. We also record wether or not a response is recieved or if it was dropped by the NAT, we denote a completed test `C-T1` and a failed test `F-T1`.

This means the mapping result will be:
```
result:        | criteria
INDEPENDENT    | PM-T1 = PM-T2 = PM-T3
ADDR_DEPENDENT | PM-T1 = PM-T2 != PM-T3
PORT_DEPENDENT | PM-T1 != PM-T2 != PM-T3
```

The Filtering result will be:
```
result:        | criteria
INDEPENDENT    | C-T4, C-T5
ADDR_DEPENDENT | C-T4, F-T5
PORT_DEPENDENT | F-T4, F-T5
```

`T6` is used determine the routers external port on the socket used for detecting filtering and defect firewall setting.


A router is deemed defect if `PM-T6 != PM-T7` and the mapping is `INDEPENDENT` If the mapping is not independent it does not make sense to do the test.
