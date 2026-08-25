# Explanation

Why this firmware is shaped the way it is. These pages exist so the reasoning
behind the awkward decisions survives, and so nobody has to rediscover a
constraint the hard way.

Read them in roughly this order:

1. **[What ports from a Flipper, and what cannot](what-ports.md)** — the honest
   accounting of which features are reachable on this hardware and which are
   not. Start here; it frames everything else.
2. **[The PA7 conflict](pa7-conflict.md)** — the single most consequential fact
   about this board. The display's data line and the Ethernet PHY's carrier
   sense are the same pin.
3. **[Why the framebuffer is 4 bits per pixel](why-4bpp.md)** — a truecolour
   buffer is 300 KB and there are 192 KB of DMA-capable RAM. What follows from
   that.
4. **[Why dirty rectangles are mandatory](dirty-rectangles.md)** — a full screen
   costs 448 ms, measured. Everything about the drawing model follows.
5. **[Clean room, and what that means here](clean-room.md)** — what was borrowed,
   what was not, and the licensing consequence.

Separately, on how the build itself is set up rather than how the firmware is
shaped: **[The toolchain, and why it comes from vcpkg](toolchain.md)** — why the
compiler is `arm-none-eabi-gcc`, where it is provisioned from and what that does
and does not pin, and the one piece of the rationale that was never written down.
