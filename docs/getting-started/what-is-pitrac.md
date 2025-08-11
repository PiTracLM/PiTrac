---
title: What is PiTrac?
layout: default
nav_order: 1
parent: Getting Started
description: Learn about PiTrac, the open-source DIY golf launch monitor that measures ball speed, launch angles, and spin using affordable Raspberry Pi hardware and infrared strobe imaging.
keywords: PiTrac introduction, golf launch monitor DIY, ball speed measurement, launch angle tracking, spin rate analysis, raspberry pi camera
---

# PiTrac - What is it? And What is it Not?

Yes, of course - PiTrac is an open-source golf launch monitor that you can build yourself, that you never need to pay a subscription for, and–if you're willing–that you can add your own features to.  But, it's more than that, and we wanted to let folks know what it's all about.

PiTrac is just a starting point of a DIY launch monitor to jump off from.  It's a starting line, not a finish line.  We hope it can act as a seed that will grow further innovation.  It's still early in its development.  We don't even have left-handed golfer support yet. :/

PiTrac is a fun build journey.  By building one of these systems, you'll push yourself through 3D printing, soldering, scripting, large(ish) software system build tools, linux utilities, web-based systems, interprocess communications, and maybe even some coding. PiTrac is a project that is sufficiently complicated and uses enough technologies that it can be a great learning platform.  If someone with few tech skills (but enthusiasm and a willingness to learn new things) took on building a PiTrac, they would come out the other end with a pretty good introduction to everything from 3D printing to Linux to building custom hardware.

PiTrac is something you can build without a full stable of equipment.  We've tried to design things so that, for example, you can 3D print the parts on the type of small-bed 3D printer you can find at many public libraries nowadays.  There's only a single custom PCB part that you can generally have fabricated for a couple dollars and no surface mount chips or other things that need specialized equipment.  All of the third-party software relied upon by the system is free.

PiTrac is a photometric-based system.  We think that ball spin is pretty important from a ball-flight physics perspective.  Which is why we built a photometric system, not a radar-based system.  Some radar systems appear to work decently for spin, especially if you add little stickers to the ball, but we thought a photometric system would have the best potential for really accurate spin analysis.

PiTrac is an aggregation of several sub-projects.  Even if you're not into golf simulation, we hope that at least some parts of the code will be helpful.  Maybe you just need a 3D model for a gimbal mount for a Raspberry Pi camera?  Maybe you're a photographer into nichey high-speed droplet pictures and just want software that can trigger a flash and a camera shutter?  We hope there's something here for you.

We also hope that the open-source nature of the system will promote a better understanding of the precision and accuracy of these types of simulated sports systems.  If a few other engineers can get interested enough to pick apart the current system's shortcomings (there are many!:) and work to publish testing results, folks can get to know exactly how close the simulation is.  And hopefully work to improve it!  That sort of information isn't really available in detail from current manufacturers.

Finally, we believe PiTrac is a tiny part of a quiet grassroots innovation movement of folks who want to build and control their own technology.  Even complicated tech!  Specifically, it's a little push-back against a world where a lot of tech is available only from large organizations and where no one knows how the tech they rely on works, let alone how to build it themselves.  A gentle nudge against products that originally cost quite a lot to design and build–and were priced accordingly–but whose high retail prices have not kept up with the progression of technology.  A dream that building your own (possibly clunky) device is more satisfying than just buying that device, especially if it's otherwise financially out of reach.  And we're pretty sure that the growing number of hackerspaces, fablabs and makerspaces and other community-based digital fabrication workshops come from a similar thought-space.

## What is PiTrac ***Not***?

It's not a thing for sale.  We're not selling anything except perhaps some circuit boards that folks can get themselves from any PCB manufacturer.  Who knows, though – maybe someone will base a business on this technology?

It's not an outdoor system.  The infrared-strobe based foundation for PiTrac would make it pretty difficult to work outdoors when competing with the sun.  But that's a tradeoff we were willing to make.

It's certainly not a commercial-quality product.  All the things you need to do in order to product-ize a technology have yet to be done.  Half the features just baaaaaarely work. ;/  However, we're hopeful that in time (especially with the help of more people in the open source community), PiTrac can become a stable, reliable easy-to-use launch monitor.

It's not cheap, even by rich-country standards.  We hope to continually push the price down, but PiTrac still costs hundreds of dollars to build.  And that doesn't include the price of tools you might need to purchase if you don't already have them.

It's not a golf simulator.  Users will likely still need to hook PiTrac up to a simulator like GsPro or E6 (both of whom do a great job with that and at price points that many people can afford).  But who knows?  Maybe an open source simulator is in the works somewhere?

It's not simple to make or to use.  Making the system currently requires some decent technical chops, including working in Linux programming environments.

---
*Raspberry Pi is a trademark of Raspberry Pi Ltd.  The PiTrac project is not endorsed, sponsored by or associated with Raspberry Pi or Raspberry Pi products or services.*