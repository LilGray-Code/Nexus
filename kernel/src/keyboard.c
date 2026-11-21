#include "keyboard.h"

#include <stddef.h>

static bool shift_down;
static bool caps_lock;
static bool extended_scan;

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static const char normal_map[128] = {
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0a]='9',[0x0b]='0',
    [0x0c]='-',[0x0d]='=',[0x0e]='\b',[0x0f]='\t',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',
    [0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',
    [0x1a]='[',[0x1b]=']',[0x1c]='\n',
    [0x1e]='a',[0x1f]='s',[0x20]='d',[0x21]='f',[0x22]='g',
    [0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',
    [0x27]=';',[0x28]='\'',[0x29]='`',[0x2b]='\\',
    [0x2c]='z',[0x2d]='x',[0x2e]='c',[0x2f]='v',[0x30]='b',
    [0x31]='n',[0x32]='m',[0x33]=',[',[0x34]='.',[0x35]='/',
    [0x39]=' '
};

static const char shifted_map[128] = {
    [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',
    [0x07]='^',[0x08]='&',[0x09]='*',[0x0a]='(',[0x0b]=')',
    [0x0c]='_',[0x0d]='+',[0x0e]='\b',[0x0f]='\t',
    [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',
    [0x15]='Y',[0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',
    [0x1a]='{',[0x1b]='}',[0x1c]='\n',
    [0x1e]='A',[0x1f]='S',[0x20]='D',[0x21]='F',[0x22]='G',
    [0x23]='H',[0x24]='J',[0x25]='K',[0x26]='L',
    [0x27]=':',[0x28]='"',[0x29]='~',[0x2b]='|',
    [0x2c]='Z',[0x2d]='X',[0x2e]='C',[0x2f]='V',[0x30]='B',
    [0x31]='N',[0x32]='M',[0x33]='<',[0x34]='>',[0x35]='?',
    [0x39]=' '
};

void keyboard_init(void) {
    shift_down = false;
    caps_lock = false;
    extended_scan = false;
    /* Drain stale controller bytes; do not reprogram firmware-owned ports. */
    for (uint32_t i = 0; i < 32U; ++i) {
        if ((inb(0x64) & 1U) == 0) break;
        (void)inb(0x60);
    }
}

bool keyboard_poll(char *character) {
    if (character == NULL || (inb(0x64) & 1U) == 0) return false;
    const uint8_t scan = inb(0x60);
    if (scan == 0xE0U || scan == 0xE1U) { extended_scan = true; return false; }
    if (extended_scan) { extended_scan = false; return false; }

    if (scan == 0x2AU || scan == 0x36U) {
        shift_down = true;
        return false;
    }
    if (scan == 0xAAU || scan == 0xB6U) {
        shift_down = false;
        return false;
    }
    if (scan == 0x3AU) {
        caps_lock = !caps_lock;
        return false;
    }
    if ((scan & 0x80U) != 0) return false;

    const uint8_t code = scan & 0x7FU;
    char value = shift_down ? shifted_map[code] : normal_map[code];
    if (value >= 'a' && value <= 'z' && caps_lock != shift_down)
        value = (char)(value - 'a' + 'A');
    if (value == '\0') return false;
    *character = value;
    return true;
}
