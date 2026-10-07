/* Linked after PS5_Vulkan's ps5-pie-unwind.ld: boundary symbols the shared
 * console crash reporter reads (.text starts at VMA 0 in that layout, as it
 * did in the PS4 port's OpenOrbis link.x). */
PROVIDE(__text_start = ADDR(.text));
