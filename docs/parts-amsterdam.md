# BikeOS — Amsterdam parts and 3D printing guide

Researched: **13 October 2026**  
Target: **under €268 / $300**, prioritizing Amsterdam walk-in purchases and local printing.  
Status: research only; no purchases, reservations, messages or print bookings made.

## Recommendation

Start with the integrated ESP32-S3 display board, USB power bank, physical buttons, and a custom PETG enclosure/mount. Keep the iPhone responsible for GPS and navigation.

Most supporting supplies have local purchase routes. **I have not verified a walk-in Amsterdam source for the preferred Waveshare display.** The confirmed Dutch listing is in Eindhoven and requires a webshop order even for collection. Do not treat this as an Amsterdam/no-order solution.

For fabrication, try **VU Education Lab first**, then **UvA Makerspace**. Both have official 3D printing facilities, but neither public page confirms free PETG printing or automatic access for this personal project. **KM Studio** is a commercial PETG option with explicitly advertised Amsterdam collection.

## 1. Display and processor

### Preferred hardware family: Waveshare ESP32-S3-Touch-LCD-4.3B

The manufacturer documents:
- Bare board **SKU 27848**; cased board **SKU 28141 / 4.3B-BOX**.
- 4.3-inch IPS, **800×480**, **550 cd/m²**.
- Integrated ESP32-S3, **16 MB flash / 8 MB PSRAM**, BLE, ESP-IDF support.
- **5 V USB-C** power.

Source: [Waveshare documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B).

This is a normal flat LCD. A curved-looking printed shell can preserve the concept's appearance, but its 5:3 screen ratio will not reproduce the extremely wide render. Sunlight readability still needs an outdoor test. Touch can be ignored.

| Item | Supplier | Listed price incl. VAT | Listed availability | Acquisition constraint |
|---|---|---:|---|---|
| Bare 4.3B, manufacturer 27848, retailer 007782 | [TinyTronics product](https://www.tinytronics.nl/nl/development-boards/microcontroller-boards/met-wi-fi/waveshare-esp32-s3-met-4.3-inch-ips-display-met-touchscreen-(b)) | **€37.50** | Page showed four units, Eindhoven warehouse | Online order required; collection or shipping |
| Cased 4.3B-BOX, 28141 | [Manufacturer listing](https://www.waveshare.com/esp32-s3-touch-lcd-4.3b.htm?sku=28141) | Not verified in NL | Amsterdam stock not found | Unresolved |

The bare listing is not the cased version. This guide uses its €37.50 price as a budget baseline, **not an approved switch from the preferred cased board**. A printed case adds cost and work.

[TinyTronics collection policy](https://www.tinytronics.nl/nl/afhalen): weekdays 13:00–18:00, after collection confirmation. It is **not a retail shop** and does not accept purchases at the counter. A trip to Eindhoven also adds travel cost, excluded below.

### Amsterdam-only fallback for learning

[Muco's board catalogue](https://www.muco.nl/1396-boards-en-startsets) lists an ESP-WROOM-32 board, **K3.962.ESP01, €14.95**, and small-display ESP32 boards around **€19.80**, marked in stock. These may support a basic BLE experiment after checking the exact model. They do **not** replace the intended navigation display. Avoid buying a second board just to start unless we deliberately choose this path.

## 2. Complete prototype parts checklist

**Listed** means a public catalogue price/stock statement, not a physical shelf check. **Allowance** means a planning amount, not a vendor quote. Unverified items need confirmation before purchase.

| Part | Qty | What to get / purpose | Local source | Price basis / status |
|---|---:|---|---|---|
| Integrated ESP32-S3 + LCD | 1 | Preferred 4.3B family; no separate ESP32 needed | See section 1 | €37.50 bare listing; Amsterdam unresolved |
| USB power bank | 1 | Standard regulated 5 V output; 10,000 mAh is a sensible starting size | Action | Sitecom listing **€11.95**; branch stock unverified |
| USB data/power cable | 1–2 | Laptop-to-board cable plus short bike cable; match laptop ports | Muco / Action | **€5 allowance**; reuse existing cable if suitable |
| Small momentary switches | 3 | Page, select/back, brightness; dry bench prototype | Muco | 6×6 mm switch **€2 each**, listed in stock |
| Breadboard | 1 | Button and interface testing | Muco | **€10 allowance**; exact SKU/price unresolved |
| Jumper wires | 1 pack | Connections on bench; choose M/M or M/F to match actual terminals | Muco | M/M 40-pack **€5.90**, listed in stock |
| Solid hookup wire | 1 small roll | Breadboard wiring | Muco | Red 10 m roll **€4.50**, listed in stock |
| Button input interface | 0–1 | Potential 3.3 V I²C expander; only after pin/address review | Ask Muco / university lab | **€12 allowance**, local stock and final device unresolved |
| Perfboard, flexible wire, headers, connectors, heat-shrink | 1 set | Permanent button wiring, strain relief | Muco | **€15 allowance**; exact connector sizes after board selection |
| PETG enclosure and clamp printing | 1 set | Shell, mount, rear cover, button caps | VU / UvA / KM Studio | **€45 allowance**, requires STL and quote |
| Fasteners | 1 set | M3 enclosure screws/nuts; provisional M4 clamp screws, washers and locknuts | Praxis / Muco | **€15 allowance**, lengths/quantities await CAD |
| Rubber pads and gasket material | 1 set | Protect bar, resist rotation, seal joints/buttons | Praxis / Muco | **€15 allowance**, exact stock unresolved |
| Power-bank straps and retention tether | 1 set | Removable attachment and backup retention | Local hardware/cycling shop | **€10 allowance**, exact product unresolved |

Product sources:
- [Sitecom 10,000 mAh power bank — €11.95, article 3216220](https://www.action.com/nl-nl/p/3216220/sitecom-powerbank/) — includes a USB-A/USB-C cable, but the page does not verify its data capability.
- [Muco 6×6 mm button — €2, S1.178.PT01](https://www.muco.nl/schakelaars/40460-printtoets-6x6mm-354035410610.html).
- [Muco 40 M/M jumpers — €5.90, Z1.178.BREADCABLE5](https://www.muco.nl/breadboards-kabeltjes-connectoren/50052-40st-breadboard-mm-kabeltjes-20cm-4250019125188.html).
- [Muco hookup wire — €4.50, 57.445.0012](https://www.muco.nl/breadboards-kabeltjes-connectoren/134516-05mm-rood-montagedraad-stug-breadboard-10m-3000000010053.html).

The bare tactile switches are **not waterproof**. Use them for bench tests; the road enclosure needs sealed button boots/caps or appropriately rated switches. The €15 sealing allowance is provisional.

### Important button interface detail

GPIO8/9 form the shared I²C bus for touch, RTC and the onboard CH422G expander. They are not three spare button inputs. Waveshare also documents isolated DI0/DI1, which require their specified interface circuit.

For three buttons, review the schematic and then choose an external interface if necessary. An MCP23017-class expander is a candidate, not a confirmed compatible purchase: check bus addresses, pull-ups, 3.3 V supply access and conflicts first. Do not wire arbitrary switches to occupied display pins or treat isolated terminals as normal GPIO.

### Tools to borrow first

USB power/current meter, multimeter, soldering iron, solder, cutters, calipers and hex keys. Ask the university lab what is provided. Reserve **€40** if essential tools must be bought; individual prices and availability are not verified.

No separate GPS module, Bluetooth module, Raspberry Pi, motor, or Arduino starter kit is required for v1. MicroSD, wheel sensor, music controls and additional lighting are optional later purchases.

## 3. Where to buy near Amsterdam West

Proximity is assessed from the previously mentioned **The Social Hub West** area. No timed route or nearest-shop ranking has been verified.

### Muco — best first electronics stop

**Burgemeester Röellstraat 32, 1064 BP Amsterdam**  
Electronics: **020 618 3781**  
Published hours: **Mon–Fri 10:00–18:00; Sat 10:00–17:00**.  
[Official shop](https://www.muco.nl/)

Bring the list and ask for small momentary switches, breadboard, wiring, perfboard, connectors and heat-shrink. Ask whether they physically stock the exact Waveshare model or a suitable I²C input interface. Their general electronics range is confirmed; those two exact items are not.

Use this current address; older directories show a Bilderdijkstraat address.

### Action — power bank

The [official Amsterdam store directory](https://www.action.com/nl-nl/winkels/l/amsterdam-noord-holland/) lists:
- **Nieuwpoortstraat 94, 1055 RZ Amsterdam**
- **Kinkerstraat 214–222, 1053 EM Amsterdam**
- **Plein 40-45 13, 1063 KR Amsterdam**

Check the package for regulated 5 V output and test operation at normal and dim-screen load. Do not assume pass-through charging for future dynamo use.

### Praxis — mounting hardware

[Praxis City Kinkerstraat](https://www.praxis.nl/storedetail/2350/amsterdam-city-kinkerstraat): **Kinkerstraat 274, 1053 GB Amsterdam**.

Buy machine screws/bolts and matching nuts, washers, straps and padding after CAD establishes sizes. Catalogue availability is not branch stock confirmation. Use captured nuts if local heat-set inserts are unavailable; do not substitute wood screws for a designed bolt connection.

### Nearby-city backup: Display Elektronica, Haarlem

**Lange Herenstraat 18, 2011 LH Haarlem**, near the station.  
**023 532 2421**. Published hours: Tue–Fri 10:00–17:30; Sat 10:00–17:00; Mon closed.  
[Official contact page](https://display-elektronica.nl/contact/).

A genuine walk-in alternative for general components; the target LCD is not verified there.

## 4. 3D printing options

| Option | Address / contact | Verified facilities and access | What remains unresolved |
|---|---|---|---|
| **VU Education / Tech Lab — first choice** | De Boelelaan **1111**, NU building, **1A-25**, 1081 HV Amsterdam; **techlab.ub@vu.nl** | Official page lists Bambu H2D and Ultimaker S5; students can explore 3D printing | Personal project eligibility, induction, PETG, price and booking |
| **UvA Makerspace — second student route** | **Science Park 904**, room **D0.120B**; **makerspace@uva.nl**, coordinator Pim Linnebank | 3D printers and microcontrollers; existing project users have access, others contact coordinator | Your access approval, PETG, equipment training and charges |
| **KM Studio — commercial collection option** | **TT Vasumweg 101-15, 1033 SG Amsterdam**, by appointment | PETG service and free studio collection explicitly published | Exact quote, turnaround and appointment |
| **Contact Amsterdam — nearby lead with capacity warning** | **Contactweg 47, 1014 AN Amsterdam**; **rutger@contactamsterdam.nl** | Makerspace includes 3D printers | Public MakeMe service currently says it cannot accept requests; self-service membership/access needs checking |

Sources: [VU](https://vu.nl/en/about-vu/divisions/university-library/more-about/3d-printing), [UvA](https://www.uva.nl/over-de-uva/organisatie/faculteiten/faculteit-der-natuurwetenschappen-wiskunde-en-informatica/locatie-en-contact/makerspace/makerspace.html), [KM pricing and collection](https://km-studio.nl/pricing), [Contact service](https://www.contactamsterdam.nl/makeme), [Contact details](https://www.contactamsterdam.nl/contact).

KM publishes a **€90–150/kg PETG guide excluding VAT**, with setup, machine-time and handling factors. This is **not** a €9–15 all-in quote for a 100 g part. Obtain a complete VAT-inclusive quote from the STL before committing.

Contact lists €25 setup plus €4/hour, excluding material, but its capacity warning means it is **not a ready-to-book recommendation**.

### What to print

First print a low-cost size/fit mockup, then the final PETG shell and clamp. CAD must use the real display dimensions, bar/stem geometry and cable clearances.

Request:
- Black PETG shell, rear lid and padded clamp.
- Captured nuts or compatible inserts; removable access to USB.
- Gasket groove, sealed button provisions and cable strain relief.
- Print orientation and reinforcement appropriate to clamp loads.
- Quote including material, VAT, setup, all pieces and one potential revision.

A starting CAD/print proposal is 0.2 mm layers and 4–6 walls, with local solid reinforcement around bolts. Final settings depend on geometry; infill alone does not establish strength. Validate retention and rotation before road use. A printed case is not inherently waterproof or an IP-rated assembly.

## 5. Dynamo research — local shop found, purchase deferred

**De Vakantiefietser**  
**Westerstraat 216, 1015 MS Amsterdam**  
**020 616 4091**, **info@vakantiefietser.nl**  
Tue–Fri 10:00–18:00; Sat 09:00–17:00; Mon/Sun closed.  
[Official contact](https://www.vakantiefietser.nl/contact).

Their [Cycle2Charge V3+ listing](https://www.vakantiefietser.nl/product/cycle2charge) is **€109.95 and marked sold out**. It is a hub-dynamo USB converter, not a dynamo, and is not a confirmed local purchase opportunity.

Bring your bike or photos for identification of the generator and lighting circuit. Ask about an in-stock alternative, installation compatibility, light sharing and buffering.

Do not buy a new dynamo before identifying the existing equipment. Do not connect dynamo AC directly to the display. A future charger must support the identified dynamo, protect against voltage excursions and work with a verified buffer/power path. Computer operation must survive stops while the bike lights remain reliable. An ordinary power bank's simultaneous charging/output behavior cannot be assumed.

The €109.95 candidate plus the provisional complete prototype budget below would exceed €268, even before installation. Dynamo charging remains a separate scope/budget decision.

## 6. Budget: listed prices versus allowances

| Category | Amount |
|---|---:|
| Bare display + Sitecom bank + 3 buttons + jumper pack + hookup-wire roll | **€65.85** |
| Cable, breadboard, possible input interface, permanent wiring, print, fasteners, seals, straps | **€127.00 allowance** |
| Contingency | **€20.00 allowance** |
| Provisional hardware total | **€212.85** |
| Optional tool reserve | **€40.00 allowance** |
| Total including tool reserve | **€252.85** |
| Remaining against €268 | **€15.15** |

This is a planning envelope, **not a verified complete checkout total**. It assumes the €37.50 bare board, borrowed or limited tools, and printing within €45. Actual cased-board pricing, print quote, travel, shipping and paid iOS distribution costs are not included. If using an existing bank/cables, remove those costs. Record actual gross prices and receipts before claiming the project is under budget.

## 7. Next actions

1. Take cockpit, front hub and lamp-label photos; measure bar diameter, usable width and stem clearances.
2. Ask Muco about exact display stock before travelling; get local prototyping supplies once the interface is decided.
3. Ask VU about project access, PETG, tools and cost; try UvA if necessary.
4. Resolve the display sourcing gap: local stock confirmation first; Dutch webshop purchase only if Simon accepts that exception.
5. Bring up display and BLE on the bench, then finalize button wiring and CAD.
6. Obtain a print quote and update the running BOM before spending.
7. Test the mounted USB-powered prototype; characterize dynamo options afterward.

No facility has been contacted and no shelf stock or print slot has been reserved. The attached concept image was not available at its supplied scratch path, so this guide does not embed a missing image.
