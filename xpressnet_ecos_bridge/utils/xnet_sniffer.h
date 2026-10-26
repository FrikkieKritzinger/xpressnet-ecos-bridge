/*
 * XpressNet sniffer - a receive-only mode (build env `wemos_sniff`).
 *
 * The board listens on an XpressNet bus it does NOT run (for example a real
 * Z21's) and logs every message over the network log, never transmitting: the
 * MAX485 driver stays disabled. Used 2026-10-26 to learn what a real command
 * station sends a MultiMaus when another throttle changes a function.
 */

#ifndef XNET_SNIFFER_H
#define XNET_SNIFFER_H

void xnetSnifferBegin();
void xnetSnifferUpdate();

#endif  // XNET_SNIFFER_H
