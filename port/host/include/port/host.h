/**
 * Host-only entry points of the port's SDK stand-ins (port/host/src).
 */

#ifndef PORT_HOST_H
#define PORT_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

/** Directory that DVD paths resolve against (the extracted disc's files/). */
void PortHostSetDiscRoot(const char* pRoot);

#ifdef __cplusplus
}
#endif

#endif
