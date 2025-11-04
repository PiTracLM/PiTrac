---
title: 3D Printing
layout: default
nav_order: 3
parent: Hardware
description: 3D printing guide for PiTrac enclosure components including STL file downloads, printer requirements, material recommendations, and printing tips for the launch monitor housing.
keywords: 3D print golf enclosure, launch monitor 3D printing, PLA golf project, DIY enclosure printing, STL files golf
og_image: /assets/images/logos/PiTrac_Square.png
last_modified_date: 2025-01-04
---

# 3D Printing

The PiTrac enclosure components require 3D printing. The 3D models and printing instructions are available in the main PiTrac repository.

## 3D Models Location

All 3D printable parts can be found in the main PiTrac GitHub repository under:
- `3D Printed Parts/` directory

{: .highlight }
**[Download 3D Parts](https://github.com/pitraclm/pitrac/tree/main/3D%20Printed%20Parts)** - Access all STL files and 3D models on GitHub

## Printing Requirements

- **Printer bed size:** Designed for small-bed 3D printers (such as those available at public libraries)
- **Material:** PLA recommended
- **Print time:** Multi-day project due to part sizes
- **Assembly:** Some parts require post-processing and fitting

## Enclosure Versions

### Version 1 (Original)
- Bulky design
- Houses all OTS (off-the-shelf) parts and separate Pi SBCs
- May not work for larger power adapters (like U.K. bricks)

### Version 2 (Current)
- More streamlined design
- Two variants:
  1. **Compute Board variant:** For future integrated compute board
  2. **Connector Board variant:** For current OTS parts build

## Print Settings

Refer to the individual part files for specific print settings. Generally:
- Standard resolution should be sufficient
- Support material may be required for some parts
- Pay attention to part orientation for optimal strength

## Post-Processing

- Remove printing supports carefully
- Test fit before final assembly
- Sand contact surfaces if needed (especially lap joints)
- Some parts may require drilling or trimming for proper fit

For detailed printing and assembly instructions, see the [Assembly Guide]({% link hardware/assembly-guide.md %}).