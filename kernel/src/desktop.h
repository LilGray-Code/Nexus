#ifndef NEXUS_DESKTOP_H
#define NEXUS_DESKTOP_H

#include <stdbool.h>
#include <limine.h>

/* Draws the first in-kernel desktop shell on a Limine RGB framebuffer. */
bool desktop_show(const struct limine_framebuffer *framebuffer);

#endif
