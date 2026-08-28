# PiTrac Proprietary Model License Agreement

Version 1.0, April 2026

Copyright © 2026 PiTracLM. All Rights Reserved.

-----

## Definitions

“Agreement” means the terms and conditions for use of the Model Materials set forth herein.

“Model Materials” means all trained machine-learning model weights, parameters, and
serialized model files distributed within the PiTrac repository or by PiTracLM through
any channel. This includes, without limitation, all files with the extensions `.onnx`,
`.pt`, `.pth`, `.engine`, `.tflite`, `.bin`, `.param`, `.safetensors`, and any other
file format containing trained neural network weights or model graph definitions, as
well as any converted, quantized, or otherwise transformed derivatives of such files.
For the avoidance of doubt, this includes all YOLO-based object detection model weights
and ncnn model files (both `.param` graph definitions and `.bin` weight files)
distributed as part of PiTrac.

“PiTrac Software” means the open-source software, source code, documentation, hardware
designs, and other materials distributed in the PiTrac repository
(https://github.com/PiTracLM/PiTrac) under the GNU General Public License v2.0,
expressly excluding the Model Materials.

“PiTracLM” or “we” means the PiTracLM organization and its authorized maintainers.

“Licensee” or “you” means any individual or entity that accesses, downloads, or
otherwise obtains the Model Materials.

“Authorized Use” means the execution of the Model Materials solely and exclusively
as an integrated, unmodified component of the PiTrac Software, running on hardware
operated by the Licensee for the Licensee’s own personal, non-commercial use of the
PiTrac launch monitor system.

-----

## 1. Scope and Relationship to Other Licenses

The PiTrac repository contains materials governed by two separate and independent
licenses:

a. **PiTrac Software** is licensed under the GNU General Public License v2.0
(see `LICENSE` in the repository root). The GPL-2.0 applies exclusively to the
source code, documentation, hardware designs, and other non-model materials.

b. **Model Materials** are licensed exclusively under this Agreement. The GPL-2.0
does NOT apply to the Model Materials. No provision of the GPL-2.0, including but
not limited to its grant of rights to copy, distribute, or modify, shall be
construed to extend to the Model Materials in any way.

These two licenses are independent. Rights granted under one do not extend to
materials governed by the other.

-----

## 2. Grant of Rights

Subject to the terms of this Agreement, PiTracLM grants you a limited,
non-exclusive, non-transferable, non-sublicensable, revocable, royalty-free license
to use the Model Materials solely for Authorized Use as defined above.

-----

## 3. Restrictions

You shall NOT, and shall not permit or enable any third party to:

a. **Redistribute** the Model Materials in any form, whether in whole or in part,
modified or unmodified, standalone or bundled with other software or data, through
any medium including but not limited to file sharing, hosting, mirroring, torrent
distribution, package repositories, model hubs, or any other distribution channel.

b. **Extract, copy, or separate** the Model Materials from the PiTrac Software for
any purpose, including but not limited to use in other software, projects, products,
services, or research.

c. **Create derivative works** from the Model Materials, including but not limited to
fine-tuning, transfer learning, distillation, pruning, quantization (except as
performed automatically by the unmodified PiTrac Software at runtime), merging,
or any other process that uses the Model Materials as an input to produce new
model weights or parameters.

d. **Reverse engineer, decompile, or disassemble** the Model Materials, or attempt
to derive the training data, training process, model architecture beyond what is
documented in the PiTrac Software, or any trade secrets embodied in the Model
Materials.

e. **Use the Model Materials for benchmarking, evaluation, or comparison** against
other models, products, or services, or publish any performance metrics derived
from the Model Materials, without prior written permission from PiTracLM.

f. **Use the Model Materials in any commercial product or service**, whether directly
or indirectly, without prior written permission from PiTracLM.

g. **Sublicense, sell, lease, rent, loan, or otherwise transfer** the Model Materials
or any rights under this Agreement to any third party.

h. **Remove, alter, or obscure** any copyright notices, license files, metadata,
watermarks, or other proprietary markings embedded in or accompanying the Model
Materials.

-----

## 4. Intellectual Property

a. PiTracLM retains all right, title, and interest in and to the Model Materials,
including all intellectual property rights therein. No title to or ownership of
the Model Materials or any intellectual property rights therein is transferred to
you under this Agreement.

b. The Model Materials may contain proprietary watermarks, fingerprints, or other
identifiers embedded within the model weights or model files. Where present, these
watermarks constitute confidential and proprietary information of PiTracLM and
serve as evidence of ownership and provenance. Any attempt to remove, alter, or
obscure these watermarks is a violation of this Agreement. For the avoidance of
doubt, the absence of watermarks or identifiers in any Model Materials does not
diminish, limit, or otherwise affect the protections, restrictions, or rights
granted under this Agreement. All Model Materials are fully protected by this
Agreement regardless of whether they contain watermarks or identifiers.

c. You acknowledge that the Model Materials were developed through significant
investment of time, resources, and expertise by PiTracLM, and that unauthorized
use or distribution would cause irreparable harm to PiTracLM.

-----

## 5. Enforcement and Remedies

a. PiTracLM reserves the right to enforce this Agreement through all available legal
mechanisms, including but not limited to:

i. Filing Digital Millennium Copyright Act (DMCA) takedown notices or equivalent
notices under applicable law with any platform, hosting provider, or service
where the Model Materials are found in violation of this Agreement.

ii. Pursuing injunctive relief, damages, and any other remedies available under
applicable law.

b. In the event that the Model Materials are found on any platform, repository, model
hub, file-sharing service, or other distribution channel in violation of this
Agreement, PiTracLM will issue takedown requests and pursue all available remedies.

c. Any unauthorized use, reproduction, or distribution of the Model Materials may
subject you to civil liability and criminal penalties under applicable copyright
and trade secret laws.

-----

## 6. Written Permission for Other Uses

Any use of the Model Materials beyond the Authorized Use defined in this Agreement
requires explicit, prior, written permission from PiTracLM. Requests may be directed
to the PiTracLM organization through official channels as published at
https://github.com/PiTracLM. Permission, if granted, may be subject to additional
terms and conditions at PiTracLM’s sole discretion.

-----

## 7. Disclaimer of Warranty

UNLESS REQUIRED BY APPLICABLE LAW, THE MODEL MATERIALS ARE PROVIDED ON AN “AS IS”
BASIS, WITHOUT WARRANTIES OF ANY KIND, AND PITRACLM DISCLAIMS ALL WARRANTIES OF
ANY KIND, BOTH EXPRESS AND IMPLIED, INCLUDING, WITHOUT LIMITATION, ANY WARRANTIES
OF TITLE, NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
YOU ARE SOLELY RESPONSIBLE FOR DETERMINING THE APPROPRIATENESS OF USING THE MODEL
MATERIALS AND ASSUME ANY RISKS ASSOCIATED WITH YOUR USE OF THE MODEL MATERIALS AND
ANY OUTPUT AND RESULTS.

-----

## 8. Limitation of Liability

IN NO EVENT WILL PITRACLM OR ITS CONTRIBUTORS BE LIABLE UNDER ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, TORT, NEGLIGENCE, PRODUCTS LIABILITY, OR OTHERWISE,
ARISING OUT OF THIS AGREEMENT, FOR ANY LOST PROFITS OR ANY INDIRECT, SPECIAL,
CONSEQUENTIAL, INCIDENTAL, EXEMPLARY, OR PUNITIVE DAMAGES, EVEN IF PITRACLM OR ITS
CONTRIBUTORS HAVE BEEN ADVISED OF THE POSSIBILITY OF ANY OF THE FOREGOING.

-----

## 9. Term and Termination

a. The term of this Agreement commences upon your access to the Model Materials and
continues until terminated.

b. The Model Materials have been proprietary to PiTracLM at all times since their
creation. This Agreement applies to all copies of the Model Materials in your
possession or control, regardless of when or how they were obtained, including
copies obtained prior to the publication of this Agreement. No prior absence of
an explicit license file in the repository or any other distribution channel shall
be construed as a grant of rights, a waiver of copyright, or a dedication to the
public domain.

b. PiTracLM may terminate this Agreement at any time if you are in breach of any term
or condition of this Agreement. Termination is effective immediately upon notice.

c. Upon termination, you shall immediately delete all copies of the Model Materials
in your possession or control and cease all use thereof.

d. Sections 3, 4, 5, 7, 8, and 10 shall survive the termination of this Agreement.

-----

## 10. General

a. This Agreement constitutes the entire agreement between you and PiTracLM regarding
the Model Materials and supersedes all prior agreements and understandings, whether
written or oral, regarding the subject matter hereof.

b. If any provision of this Agreement is held to be unenforceable, such provision shall
be reformed only to the extent necessary to make it enforceable, and the remaining
provisions shall continue in full force and effect.

c. The failure of PiTracLM to enforce any right or provision of this Agreement shall
not constitute a waiver of such right or provision.

d. This Agreement shall be governed by and construed in accordance with the laws of
the Commonwealth of Pennsylvania, United States of America, without regard to its
conflict of laws principles. The UN Convention on Contracts for the International
Sale of Goods does not apply to this Agreement. PiTracLM may seek enforcement of
this Agreement in any court of competent jurisdiction worldwide. Nothing in this
Agreement limits PiTracLM’s right to bring proceedings in any jurisdiction where
the Licensee resides, operates, or where infringement occurs.

-----

By accessing, downloading, or using the Model Materials in any way, you acknowledge
that you have read, understood, and agree to be bound by this Agreement. If you do not
agree to these terms, you must not access, download, or use the Model Materials.