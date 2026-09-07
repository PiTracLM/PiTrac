# PiTrac Proprietary Model License Agreement

Version 1.1, August 2026

Copyright (c) 2026 PiTracLM. All Rights Reserved.

This Agreement supersedes Version 1.0 (April 2026), archived at
`LICENSE.MODEL.v1.0.md` in the PiTrac repository. See Section 11.

-----

## Definitions

"Agreement" means the terms and conditions for use of the Model Materials set forth
herein.

"Model Materials" means all trained machine-learning model weights, parameters,
serialized model files, model graph definitions, training checkpoints, and
substantially equivalent representations that have been, are, or are in future
distributed or otherwise made available by PiTracLM through any channel.

A file constitutes Model Materials if it contains trained neural network weights,
parameters, or model graph definitions, irrespective of its filename or extension.
Such files include, without limitation, those in the `.onnx`, `.pt`, `.pth`, `.engine`,
`.tflite`, `.bin`, `.param`, `.safetensors`, and `.weights` formats. A file that does
not contain trained weights, parameters, or model graph definitions is not Model
Materials by reason of its filename or extension alone.

Model Materials include, without limitation:

a. every version, revision, and release of such materials, whether current, prior,
superseded, deprecated, experimental, or removed;

b. such materials as present in any branch, tag, commit, pull request, release asset,
package, container image, or archive of any repository operated by PiTracLM, and in
the revision history of any such repository, whether or not the materials are present
at the current head of any branch;

c. such materials as present in any fork, clone, mirror, or other copy of any such
repository, however obtained;

d. training checkpoints, experiment outputs, and intermediate weights produced in the
course of developing such materials; and

e. any converted, quantized, exported, transformed, or otherwise derived
representation of any of the foregoing.

For the avoidance of doubt, Model Materials include all YOLO-based object detection
weights, all ncnn model files (both `.param` graph definitions and `.bin` weight
files), all ball detection and spin prediction models, and all training checkpoints
and experiment weights, in every revision in which they appear.

The removal of Model Materials from the current state of any branch, repository, or
distribution channel does not remove those materials from the scope of this Agreement,
and confers no right to recover, extract, copy, or use materials obtainable from
revision history or from any prior revision.

"Training Materials" means the datasets, images, video, annotations, labels, dataset
configurations, training hyperparameters, training configurations, evaluation results,
and other materials created or assembled by PiTracLM and used to produce the Model
Materials, together with any subset, extract, or derivative of them.

Training Materials include such materials wherever they appear, including in the
revision history of any repository operated by PiTracLM, in any fork, clone, mirror, or
other copy of such a repository, and whether or not they are present at the current head
of any branch.

Training Materials do not include source code distributed as part of the PiTrac Software
under GPL-2.0.

Except where this Agreement provides otherwise, every reference to the Model Materials
in Sections 3, 3A, 4, 5, 9, and 11 includes the Training Materials. No provision of
Section 2 grants any right in the Training Materials.

"PiTrac Software" means the open-source software, source code, documentation, hardware
designs, and other non-model materials distributed by PiTracLM under the GNU General
Public License v2.0 ("GPL-2.0"), expressly excluding the Model Materials.

"PiTrac Fork" means a modification, derivative work, adaptation, or redistribution of
the PiTrac Software made in accordance with the rights granted under GPL-2.0. A PiTrac
Fork does not include, and no rights in a PiTrac Fork extend to, the Model Materials.

"PiTracLM" or "we" means the PiTracLM organization and its authorized maintainers.

"Licensee" or "you" means any individual or entity that accesses, downloads, or
otherwise obtains the Model Materials.

"Commercial Offering" means any product, service, appliance, system, software
distribution, installer, integration, setup service, support arrangement, or other
offering that is sold, leased, licensed, rented, provided for a fee, monetized, or
otherwise supplied for direct or indirect commercial advantage, and that:

a. includes or incorporates the Model Materials;

b. downloads, retrieves, installs, provisions, configures, or activates the Model
Materials;

c. directs, causes, or enables an end-user device to download, retrieve, install,
provision, configure, or activate the Model Materials;

d. is supplied in a state in which the Model Materials are present on, or are
configured to be acquired by, the item supplied; or

e. materially depends upon the Model Materials to provide its advertised, intended, or
reasonably expected functionality.

An offering may constitute a Commercial Offering regardless of whether the Model
Materials are separately charged for, whether software included with the offering is
provided without charge or under an open-source license, or whether the Model Materials
are obtained directly from PiTracLM rather than passing through the commercial
provider's possession, servers, repositories, or infrastructure.

For the avoidance of doubt, the sale or supply of enclosures, printed circuit boards,
camera mounts, lighting assemblies, fasteners, printed parts, or other physical
components manufactured from hardware designs distributed under GPL-2.0 or another
open-source or open-hardware license does not by itself constitute a Commercial
Offering, provided the components are supplied without the Model Materials present and
without software configured to acquire them.

"Authorized Use" means use and execution of the Model Materials by an individual
solely:

a. with the PiTrac Software or a PiTrac Fork;

b. on hardware owned, possessed, or operated by that individual;

c. for that individual's own personal, non-commercial use; and

d. other than as part of, or in connection with, an unauthorized Commercial Offering.

Use of a PiTrac Fork rather than the PiTrac Software does not by itself make use of the
Model Materials unauthorized.

-----

## 1. Scope and Relationship to GPL-2.0

The PiTrac Software and the Model Materials are separate works governed by separate and
independent licenses.

a. The PiTrac Software is licensed under GPL-2.0 (see `LICENSE` in the repository
root). Nothing in this Agreement limits, restricts, conditions, or modifies any right
granted under GPL-2.0 with respect to the PiTrac Software, including the rights to use,
study, modify, fork, reproduce, or redistribute the PiTrac Software in accordance with
GPL-2.0.

b. The Model Materials are not licensed under GPL-2.0. No provision of GPL-2.0,
including its grant of rights to copy, distribute, or modify, extends to the Model
Materials. No right granted with respect to the PiTrac Software or any PiTrac Fork
grants, implies, or creates any right to copy, distribute, modify, sublicense,
commercially exploit, or otherwise use the Model Materials except as expressly provided
by this Agreement.

c. For the avoidance of doubt, any person may create, maintain, publish, and
redistribute a PiTrac Fork in accordance with GPL-2.0 without permission from PiTracLM.
Any use or acquisition of the Model Materials in connection with that PiTrac Fork
remains subject to this Agreement.

-----

## 2. Grant of Rights

Subject to the terms of this Agreement, PiTracLM grants each Licensee a limited,
non-exclusive, non-transferable, non-sublicensable, revocable, royalty-free license to
possess and execute the Model Materials solely for Authorized Use.

Authorized Use expressly includes personal, non-commercial use of the Model Materials
with the PiTrac Software and with PiTrac Forks.

This license is personal to the Licensee. It does not run with any device, is not
conveyed by the sale or transfer of hardware, and is not acquired by a subsequent
possessor of hardware on which the Model Materials are present.

Except for the foregoing limited license, no rights in the Model Materials are granted,
whether expressly, by implication, estoppel, exhaustion, or otherwise.

-----

## 3. Restrictions

Except with PiTracLM's explicit prior written permission, you shall NOT, and shall not
permit, direct, authorize, induce, or enable any third party to:

a. **Redistribute the Model Materials.** Copy, distribute, publish, transmit, upload,
host, mirror, sublicense, provide, transfer, or otherwise make the Model Materials
available to another person or entity, whether in whole or in part, modified or
unmodified, standalone or bundled, through a repository, package, container image, disk
image, archive, installer, appliance, server, peer-to-peer service, model hub, or any
other distribution mechanism.

Publishing, hosting, or distributing a repository, archive, image, or other copy whose
revision history contains the Model Materials makes the Model Materials available
within the meaning of this paragraph. This is so whether or not the Model Materials are
present at the current head of any branch, whether or not they can be obtained without
checking out a prior revision, and whether or not the publisher is aware of their
presence in that history.

This paragraph does not prohibit a PiTrac Fork from retaining and republishing the
revision history of a PiTracLM repository where retention of that history is incidental
to exercising rights granted under GPL-2.0. That allowance extends to retention of
history alone. It confers no right to extract, install, provision, execute, or
otherwise use the Model Materials, which remains governed by Sections 2, 3, and 3A. The
allowance is conditioned on continued compliance with GPL-2.0 in respect of the PiTrac
Software, and ceases as to any project that is not, or ceases to be, a PiTrac Fork.

A project that is not a PiTrac Fork shall not be published or distributed with revision
history containing the Model Materials. Where such a project originates from a clone,
fork, or other copy of a PiTracLM repository, the Model Materials shall be removed from
its revision history before it is published or distributed.

b. **Commercially use the Model Materials.** Use, incorporate, execute, deploy, supply,
or otherwise exploit the Model Materials in connection with a Commercial Offering.

c. **Commercially provision or facilitate the Model Materials.** As part of or in
connection with a Commercial Offering, create, distribute, supply, operate, or use any
mechanism that automatically or programmatically downloads, retrieves, fetches,
installs, provisions, configures, activates, or otherwise obtains or enables use of the
Model Materials for another person or on another person's device.

This restriction applies even where:

i. the Model Materials are downloaded directly from a repository, URL, server, API, or
other endpoint operated by PiTracLM;

ii. the Model Materials never pass through the commercial provider's servers or
infrastructure;

iii. the commercial provider never possesses a persistent copy of the Model Materials;

iv. the download or installation occurs only after delivery of hardware or software to
an end user;

v. the end user initiates, approves, or completes the download;

vi. the end user separately accepts this Agreement; or

vii. the software or script performing the download is itself distributed under
GPL-2.0, the MIT License, or any other open-source license.

The absence of the Model Materials from a Commercial Offering at the time of sale,
shipment, delivery, installation, or transfer does not exempt the offering from this
restriction where the offering is designed, configured, marketed, or supplied to obtain
or enable use of the Model Materials afterward.

d. **Circumvent the restrictions in this Agreement.** Structure, divide, characterize,
or arrange a transaction, distribution, installation process, or technical workflow for
the purpose or effect of accomplishing indirectly what this Agreement prohibits
directly. Separating hardware, software, installation, model acquisition, activation,
or related services into separate steps, transactions, entities, downloads,
repositories, or components does not make an otherwise prohibited Commercial Offering
an Authorized Use.

e. **Extract, copy, or separate the Model Materials.** Extract, copy, or separate the
Model Materials for use outside the PiTrac Software or a PiTrac Fork, except for
temporary or incidental copies technically necessary for Authorized Use.

f. **Create derivative Model Materials.** Fine-tune, retrain, transfer-learn, distill,
prune, merge, quantize, convert, transform, or otherwise use the Model Materials to
create new or modified model weights, parameters, or substantially equivalent model
representations, except for transformations performed by the PiTrac Software as part of
Authorized Use. You shall not use any output, inference, prediction, label, annotation,
embedding, or intermediate representation generated by the Model Materials as training
data, as a supervision or distillation signal, or as any other input to the creation,
training, fine-tuning, or evaluation of any machine-learning model.

g. **Reverse engineer the Model Materials.** Reverse engineer, decompile, disassemble,
or otherwise attempt to derive training data, non-public training methods, model
architecture beyond what is documented in the PiTrac Software, or other proprietary
information or trade secrets embodied in the Model Materials.

h. **Benchmark or publish metrics.** Use the Model Materials for benchmarking,
evaluation, or comparison against other models, products, or services, or publish any
performance metrics derived from the Model Materials.

i. **Transfer rights.** Sell, sublicense, lease, rent, loan, assign, or otherwise
transfer the Model Materials or any rights granted under this Agreement to any third
party.

j. **Remove proprietary markings.** Remove, alter, or obscure any copyright notices,
license files, metadata, watermarks, fingerprints, or other proprietary markings
embedded in or accompanying the Model Materials.

-----

## 3A. Acquisition of the Model Materials

The Model Materials may be obtained only by the individual who will use them, acting
directly, from a distribution channel operated by PiTracLM, or through the official
PiTrac installer as published by PiTracLM.

No other software, script, installer, agent, service, appliance, or automated mechanism
may download, retrieve, fetch, install, provision, configure, or activate the Model
Materials on behalf of any person. This prohibition applies regardless of whether:

a. the software is distributed for charge or without charge;

b. the software is a PiTrac Fork, an independent application, or a component of either;

c. the software is distributed under GPL-2.0, the MIT License, or any other license;

d. the Model Materials are obtained directly from an endpoint operated by PiTracLM;

e. the person is shown this Agreement or separately accepts it; or

f. the person initiates, approves, or completes the acquisition.

Nothing in this Section limits the right to create, maintain, publish, or redistribute
a PiTrac Fork in accordance with GPL-2.0. A PiTrac Fork or other software may direct a
user to PiTracLM's official distribution channel, may document how to obtain the Model
Materials, and may detect whether the Model Materials are already present on a system.
It may not acquire them.

Circumventing, disabling, or bypassing any authentication, access control,
license-acceptance mechanism, download restriction, rate limit, or other technical
measure imposed by PiTracLM in connection with distribution of the Model Materials is
prohibited, and terminates this Agreement as to the party responsible under Section
9(c).

-----

## 4. Intellectual Property

a. PiTracLM retains all right, title, and interest in and to the Model Materials,
including all intellectual property rights therein. No title to or ownership of the
Model Materials or any intellectual property rights therein is transferred to you under
this Agreement.

b. The training data from which the Model Materials were derived contains proprietary
markers embedded by PiTracLM. As a consequence of having been trained on that data, the
Model Materials exhibit characteristic and identifiable responses when presented with
that material. These markers, the material that elicits such responses, and the
responses themselves constitute confidential and proprietary information and trade
secrets of PiTracLM, and serve as evidence of ownership and provenance. Any attempt to
remove, alter, obscure, defeat, or evade these markers, or to suppress or mask the
responses they elicit, is a violation of this Agreement. For the avoidance of doubt, the
inability to elicit or detect such a response from any particular Model Materials does
not diminish, limit, or otherwise affect the protections, restrictions, or rights
granted under this Agreement. All Model Materials are fully protected by this Agreement
regardless of whether such a response is present or detectable.

c. You acknowledge that the Model Materials were developed through significant
investment of time, resources, and expertise by PiTracLM, and that unauthorized use,
provisioning, or distribution would cause irreparable harm to PiTracLM.

d. A model trained on, fine-tuned from, distilled from, or otherwise derived from the
Model Materials, and a model trained on outputs, inferences, predictions, labels, or
annotations generated by the Model Materials, inherits the characteristic responses
described in paragraph (b). You acknowledge and agree that:

i. eliciting such a response from any model, dataset, or system is evidence that the
Model Materials, or the training data from which they were derived, were used in its
creation or training;

ii. you will not contest the admissibility of such evidence in any proceeding brought
to enforce this Agreement;

iii. PiTracLM may submit inputs to, query, test, analyze, and inspect any model,
dataset, or system that it reasonably believes was created or trained in violation of
Section 3(f), for the purpose of eliciting or detecting such a response; and

iv. you will not restrict, block, throttle, or otherwise interfere with PiTracLM's
ability to conduct the testing described in subparagraph (iii), whether by terms of
service, access control, rate limiting, or any other means.

Nothing in this paragraph limits PiTracLM's ability to establish use of the Model
Materials by any other means, and no failure to elicit or detect such a response shall
be construed as evidence that the Model Materials were not used.

-----

## 5. Enforcement and Remedies

a. PiTracLM reserves the right to enforce this Agreement through all available legal
mechanisms, including but not limited to:

i. Filing Digital Millennium Copyright Act (DMCA) takedown notices or equivalent
notices under applicable law with any platform, hosting provider, or service where the
Model Materials are found in violation of this Agreement.

ii. Pursuing injunctive relief, damages, and any other remedies available under
applicable law.

b. In the event that the Model Materials are found on any platform, repository, model
hub, file-sharing service, or other distribution channel in violation of this
Agreement, PiTracLM will issue takedown requests and pursue all available remedies.

c. PiTracLM may pursue any party that induces, contributes to, materially assists, or
provides the means for another party's violation of this Agreement, including by
authoring, publishing, distributing, or operating software or instructions that acquire
or provision the Model Materials in violation of Section 3 or Section 3A. Liability
under this paragraph does not require that the party itself have copied, possessed, or
executed the Model Materials.

d. PiTracLM may require a Licensee to certify in writing the disposition of all copies
of the Model Materials in that Licensee's possession or control, and to confirm
compliance with Section 9(d).

e. Any unauthorized use, reproduction, provisioning, or distribution of the Model
Materials may subject you to civil liability and criminal penalties under applicable
copyright and trade secret laws.

-----

## 6. Written Permission for Commercial and Other Uses

Any use of the Model Materials outside Authorized Use requires PiTracLM's explicit
prior written permission. This includes, without limitation, use of the Model Materials
in or in connection with:

a. commercially sold PiTrac or PiTrac-compatible systems;

b. preconfigured or preassembled hardware;

c. paid installation, setup, calibration, or integration services;

d. commercial installers or provisioning systems;

e. hosted or managed services;

f. products that obtain the Model Materials after sale or delivery;

g. products that instruct, assist, or automate an end user's acquisition of the Model
Materials; and

h. any other Commercial Offering that depends upon or facilitates use of the Model
Materials.

Permission to use, modify, or commercially distribute the PiTrac Software under GPL-2.0
does not constitute permission to use, provision, facilitate, distribute, or otherwise
exploit the Model Materials.

Requests may be directed to the PiTracLM organization through official channels as
published at https://github.com/PiTracLM. Permission, if granted, may be subject to
additional terms and conditions at PiTracLM's sole discretion. Commercial licenses and
other permissions may be granted separately by PiTracLM in writing.

-----

## 7. Disclaimer of Warranty

UNLESS REQUIRED BY APPLICABLE LAW, THE MODEL MATERIALS ARE PROVIDED ON AN "AS IS"
BASIS, WITHOUT WARRANTIES OF ANY KIND, AND PITRACLM DISCLAIMS ALL WARRANTIES OF ANY
KIND, BOTH EXPRESS AND IMPLIED, INCLUDING, WITHOUT LIMITATION, ANY WARRANTIES OF TITLE,
NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. YOU ARE SOLELY
RESPONSIBLE FOR DETERMINING THE APPROPRIATENESS OF USING THE MODEL MATERIALS AND ASSUME
ANY RISKS ASSOCIATED WITH YOUR USE OF THE MODEL MATERIALS AND ANY OUTPUT AND RESULTS.

-----

## 8. Limitation of Liability

IN NO EVENT WILL PITRACLM OR ITS CONTRIBUTORS BE LIABLE UNDER ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, TORT, NEGLIGENCE, PRODUCTS LIABILITY, OR OTHERWISE, ARISING OUT OF
THIS AGREEMENT, FOR ANY LOST PROFITS OR ANY INDIRECT, SPECIAL, CONSEQUENTIAL,
INCIDENTAL, EXEMPLARY, OR PUNITIVE DAMAGES, EVEN IF PITRACLM OR ITS CONTRIBUTORS HAVE
BEEN ADVISED OF THE POSSIBILITY OF ANY OF THE FOREGOING.

-----

## 9. Term and Termination

a. The term of this Agreement commences upon your access to the Model Materials and
continues until terminated.

b. The Model Materials have been proprietary to PiTracLM at all times since their
creation. This Agreement applies to all copies of the Model Materials in your
possession or control, regardless of when or how they were obtained, including copies
obtained prior to the publication of this Agreement or of any prior version of it. No
prior absence of an explicit license file in the repository or any other distribution
channel shall be construed as a grant of rights, a waiver of copyright, or a dedication
to the public domain.

c. PiTracLM may terminate this Agreement at any time if you are in breach of any term
or condition of this Agreement. Termination is effective immediately upon notice. This
Agreement terminates automatically, without notice, as to any party that authors,
publishes, distributes, or operates software or instructions in violation of Section 3A.

d. Upon termination, you shall immediately delete all copies of the Model Materials in
your possession or control, including copies present in installed directories, source
trees, build artifacts, container images, backups, and version control history, and
shall cease all use thereof.

e. Sections 3, 3A, 4, 5, 7, 8, 10, and 11 shall survive the termination of this
Agreement.

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

d. This Agreement shall be governed by and construed in accordance with the laws of the
Commonwealth of Pennsylvania, United States of America, without regard to its conflict
of laws principles. The UN Convention on Contracts for the International Sale of Goods
does not apply to this Agreement. PiTracLM may seek enforcement of this Agreement in
any court of competent jurisdiction worldwide. Nothing in this Agreement limits
PiTracLM's right to bring proceedings in any jurisdiction where the Licensee resides,
operates, or where infringement occurs.

-----

## 11. Versions of this Agreement

a. PiTracLM may publish revised versions of this Agreement. The canonical location of
the current version is
https://github.com/PiTracLM/PiTrac/blob/main/LICENSE.MODEL.md.

b. The version of this Agreement published at that location at the time of your access
to, acquisition of, or use of the Model Materials governs that access, acquisition, or
use. Continued possession or use of the Model Materials after publication of a revised
version constitutes acceptance of the revised version.

c. If you do not accept the current version, you must cease all use of the Model
Materials and delete all copies in your possession or control in accordance with
Section 9(d).

d. Superseded versions are retained in the PiTrac repository for reference. A
superseded version does not grant any right that the current version withholds.

-----

By accessing, downloading, or using the Model Materials in any way, you acknowledge
that you have read, understood, and agree to be bound by this Agreement. If you do not
agree to these terms, you must not access, download, or use the Model Materials.
