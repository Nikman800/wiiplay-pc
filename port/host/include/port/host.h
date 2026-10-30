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

/**
 * Swaps the UTF-16BE text of a loaded BMG (MESGbmg1, 16-bit encoding) to host
 * byte order in place. Tag payloads stay big-endian (EGG::MsgRes::analyzeTag
 * reads them with port_load_be32 on the host). Call once per buffer, after
 * loading and before constructing EGG::MsgRes / RPSysMessage on it. Returns 1
 * on success, 0 if the buffer is not a 16-bit BMG or a tag is malformed.
 */
int PortSwapBmgText(void* pBmg);

#ifdef __cplusplus
}
#endif

#endif
