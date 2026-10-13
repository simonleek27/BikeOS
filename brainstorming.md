# BikeOS — Brainstorming & AI Collaboration

> Status: concept and architecture exploration, not a frozen hardware specification.  
> Date: October 13, 2026  
> Budget: **under $300 (approximately €268)**, with parts sourced in the Netherlands/EU.  
> Collaborators: Simon, ND, ChatGPT (GPT-6), and Claude.

## Hey ND and Claude 👋

Hi ND! Hi Claude! I'm **ChatGPT (GPT-6)**, working with Simon on BikeOS: an actual buildable bicycle navigation computer, not just a concept render. Great to have another human/AI team involved.

I can focus on embedded firmware, ESP32 hardware integration, BLE protocol design, electrical and dynamo power planning, mechanical/enclosure CAD requirements, and end-to-end testing. Claude could lead on the SwiftUI iPhone application and review our firmware decisions. These aren't rigid assignments: let's cross-review each other's ideas and agree on shared interfaces before coding.

**Please challenge assumptions.** We want a reliable, cost-conscious, open-source portfolio project. Nobody has purchased parts yet, and we should verify feasibility, specifications, power, supplier availability, and fit before ordering.

## 1. The design Simon selected

![Preferred BikeOS integrated handlebar concept](assets/bikeos_handlebar_reference.png)

**Design intent:** a **low-profile, wide, integrated-looking navigation display mounted directly on the center of the handlebar/stem**, rather than a computer floating above the bars. Matte black shell, slight visual curvature, tidy cable management, small physical buttons at the ends (no touchscreen needed), quick-glance speed/map/next-turn layout.

The reference is an **AI-generated appearance concept, not CAD or proof of manufacturability**. The rendered screen has a dramatically wide aspect ratio; available budget ESP32 LCDs are usually closer to 800×480. We must design for real display dimensions, mount strength, common bar diameters, clearances, weather seals, and the user's actual bike. A curved outer shell or cover is practical; a **truly flexible/curved display is not assumed**.

The photo depicts drop handlebars, but we must **not assume Simon owns a drop-bar bike**. Ask for a real photo of his cockpit and measurements before finalizing the mount.

### What the on-bike UI should show

- Central street map with an obvious current-position/route indicator (stretch goal; see mapping constraint below).
- Large next-turn arrow, distance, street name, and rerouting status.
- Current speed (km/h), trip time, route distance remaining, optionally average speed/elevation.
- BLE connection state and battery/power status.
- Simple hardware buttons for page switching, brightness and selection; no touchscreen requirement.
- Bright enough for daylight; readable at a glance, with low distraction.

## 2. Product architecture — the iPhone is the navigation brain

**iPhone (SwiftUI + MapKit + CoreLocation + CoreBluetooth):**
1. Search for a cycling destination, select/start route.
2. Calculate cycling directions, receive GPS and derive speed, trip distance, navigation progress and ETA.
3. Detect off-route travel and request reroutes.
4. Send small, structured updates to the bike over **Bluetooth Low Energy (BLE)**.
5. Maintain a ride session with screen locked where iOS background permissions allow; **test on a real iPhone**, not only the simulator.

**Bike computer (ESP32-S3, ESP-IDF/C++, LVGL):**
1. Advertise a custom BLE GATT service; receive and validate phone updates.
2. Render the screen at practical frame rates with strong readability.
3. Respond to physical buttons and show connection/reconnection conditions.
4. Run trip UI/status with as little power as we can manage.
5. Later: optional wheel sensors, lights, weather, music control, offline maps.

**Important:** this is **not AirPlay mirroring Apple Maps**. An ESP32 does not simply receive the iPhone's map pixels as an AirPlay display. For v1, show turn instructions and ride stats; for a full ESP32 street map, we need a separately sourced, license-compliant map/route rendering approach (likely simplified/preprocessed map data, not continuously mirroring MapKit).

### Initial BLE protocol planning

Define a shared `protocol/` specification (versioning, units, optional fields, packet sizes, GATT characteristics, sequence numbers, timestamps, stale-data behavior). Suggested logical fields: `speed_kmh`, `distance_to_turn_m`, `turn_type`, `street_name`, `distance_remaining_m`, `ride_elapsed_s`, `navigation_state`, `phone_battery_pct`. This is a **logical schema**, not a final on-wire format. BLE payloads must respect negotiated MTU/fragmentation limitations. Plan for disconnects, stale GPS, and resume after phone lock.

## 3. Screen choices — research, don't order yet

**Leading candidate:** [Waveshare ESP32-S3-LCD-4.3 (non-touch variant)](https://www.waveshare.com/esp32-s3-touch-lcd-4.3.htm), **4.3 inches, 800×480**; Waveshare lists approximate board dimensions **106.1×67.8 mm** for the 4.3-inch family. Verify the exact non-touch SKU, sunlight readability, exposed GPIOs, connector access, backlight power, and Netherlands/EU delivered price.

**Alternative:** [Waveshare ESP32-S3-Touch-LCD-4.3B](https://www.waveshare.com/esp32-s3-touch-lcd-4.3b.htm?sku=28141), which we originally considered, including a cased variant. It **includes touch hardware**, even though we would ignore touch. Its documentation lists **800×480**, **550 cd/m²**, **112.4×75.1 mm bare / 116.3×79 mm cased**, and around **5 V × 450 mA = 2.25 W** for the reference board. These numbers **must not be assumed for the different non-touch board**.

Other candidates should include smaller 2.8–4-inch ESP32-friendly screens and, if the wide integrated format truly matters, a search for a real *wide-aspect* panel with a known ESP32-compatible display controller and validated software support. Avoid prematurely committing to a curved OLED.

**Selection criteria:** active-viewing dimensions, readable brightness in direct sunlight, power, interface/ESP-IDF support, RAM and refresh capability, weatherproofing, GPIO/button support, price and shipping, mounting/package dimensions, source documentation.

## 4. Dynamo charging — potentially the most interesting feature

Simon says **his bike's light turns on as he pedals**. This suggests an existing generator (possibly a **front-hub dynamo** or **bottle dynamo**), so **do not buy a new dynamo before inspecting the bike**. It could alternatively be an automatic battery lamp. We need photographs of the **front hub, the light, wiring and any model labels**, plus a description of whether lighting works at a standstill.

### Electrical constraints

A common bicycle hub dynamo is nominally around **6 V AC / 3 W** at specified riding conditions, but open-circuit voltage may be much higher and output depends on speed and load. Many front/rear lights already use much of that budget. The **lighting must remain dependable and have priority over computer charging**, especially at night.

**Never connect the dynamo, lamp wires, or unregulated AC directly to ESP32 GPIO or USB power. Never splice the riding-light circuit without verifying safety and behavior.**

Research a suitable architecture:

`existing dynamo → properly rated rectifier + overvoltage protection → regulated charging/power path → compatible buffer battery or supercapacitor → clean 5 V supply → BikeOS`

If the light and computer share a dynamo, we need a verified design with safe light-priority behavior (potentially a switch/selectable charging path). Power buffering is essential so the device works at traffic lights. Many ordinary USB banks cannot safely/consistently charge and discharge simultaneously; verify the specific power-path design.

At **~2.25 W** for the *4.3B* benchmark, a nominal 3 W dynamo **cannot be assumed** to both power the LCD and maintain full lighting after converter losses. Treat on-bike energy harvesting as an **optional later milestone**, with the initial prototype powered by a common USB battery bank.

### Investigate these power questions

- What generator is already installed? Manufacturer, model, label, nominal output, connector, output waveform.
- What do the front/rear lights draw and how are they wired?
- How many watts are available at realistic city speeds, with the lights on/off?
- What is the board's measured power at different screen brightness levels and BLE states?
- Can a dynamo USB charger safely share the lighting circuit? What buffer battery/charging controller is suitable?
- What are affordable NL/EU options, with real shipping, ingress protection, and safety documentation?
- Can we log dynamo input and charge/battery state to show a neat **"generated energy"** metric on BikeOS?

## 5. Mechanical design and fabrication

Aim for a **removable, adjustable, 3D-printed PETG enclosure** with low-profile handlebar/stem integration. Explore two-piece shells, heat-set inserts, gaskets, physical sealed buttons, drainage/water resistance, cable strain relief, and a mechanically strong clamp that won't rotate when hitting bumps. Avoid interfering with brake cables, steering, rider grip, lights, or stem fasteners. Never weaken a safety-critical handlebar/stem component to integrate the computer.

Investigate student-accessible prototyping at **VU Amsterdam** and **UvA**, plus local fab labs/makerspaces/3D-print services. Obtain the actual bike dimensions before CAD: bar diameter and exposed flat length, stem type, preferred angle, light/cable clearance. For a public portfolio, preserve CAD source, drawings, dimension checks, and test photos.

## 6. Initial components to source (research backlog, not a confirmed BOM)

| Priority | Part / subsystem | What must be verified |
|---|---|---|
| P0 | ESP32-S3 + non-touch color display | Exact model/SKU, delivered NL/EU price, brightness, ESP-IDF support, exposed IO |
| P0 | USB battery bank and cable | Reliable run time, 5 V stability, safe mounting |
| P0 | 3–4 physical switches, connectors, wiring | Waterproof buttons; GPIO compatibility with selected board |
| P0 | Prototyping essentials | USB data cable, breadboard/jumpers, optional voltage/current meter |
| P1 | PETG print + clamp hardware | 3D printing access, fasteners, rubber pads/gasket, mounting dimensions |
| P1 | Weatherproof housing details | Connectors, seals, vibration protection, removable charging access |
| P2 | Existing dynamo identification | Photos, output rating, existing lights and wiring |
| P2 | Dynamo USB/power-management path | Rectification, transient protection, regulation, buffer, lighting priority |
| P3 | Optional sensors/lighting | Wheel speed sensor, indicators, brake light, weather; not needed for v1 |

Maintain a **BOM with vendor URLs, gross prices incl. Dutch VAT, shipping, availability date and running total**. Keep project hardware costs **below $300 (~€268)**, with a separate column for optional energy harvesting and optional equipment/tools. Do not invent prices or claim compatibility until checked.

Useful EU supplier categories to investigate: Waveshare directly and EU resellers; TinyTronics, Kiwi Electronics, Conrad NL, 3DJake, EU bicycle dynamo/lighting dealers and student fabrication facilities.

## 7. Suggested build sequence and acceptance tests

1. **Architecture + parts decision:** verify actual display variant, bike photos, power budget, mechanical envelope and BOM.
2. **ESP32 screen bring-up:** hello BikeOS and responsive hardware buttons using ESP-IDF + LVGL.
3. **BLE handshake:** iPhone finds, connects and updates a value; reconnect reliably after interruption.
4. **Live ride metrics:** real iPhone GPS → BLE speed/distance; compare results during a short test.
5. **Routing:** phone destination selection and directions → real next-turn display; validate rerouting.
6. **Background reliability:** lock phone, ride and test BLE/GPS on physical iPhone; recover from disruptions.
7. **Mechanical prototype:** printed case and clamp, vibration clearance, day/night readability, rain protection.
8. **Optional maps:** add legally usable local map display once navigation works.
9. **Optional dynamo assist:** electrically characterize the generator and verify charging without dimming the lights.

Deliver demonstrable working milestones, not mockups alone. Maintain code, wiring diagrams, CAD, ride-test logs and failure notes for an open-source engineering portfolio.

## 8. Working together — ND, Claude and ChatGPT

Use this GitHub repository as the **shared source of truth**. Proposed future layout (not created yet):

- `ios/` — SwiftUI app, CoreLocation, MapKit, CoreBluetooth (Claude can lead)
- `firmware/` — ESP-IDF C++, BLE GATT, LVGL (ChatGPT can lead)
- `protocol/` — BLE contracts shared by both, jointly reviewed
- `hardware/` — enclosure CAD, wiring, power design
- `docs/` — BOM, requirements, testing, decisions

We cannot magically talk between AI accounts in real time. ND and Simon can ask us to inspect shared commits, review pull requests, leave technical feedback and iterate. Prefer small branches, documented decisions and tested interfaces.

**Claude, please start by reviewing these assumptions and identifying iOS-specific blockers, especially background navigation and route data sharing. ND, please challenge the mechanical integration and dynamo power budget. I'll take the first pass at screen/ESP32 shortlist, electrical constraints and the hardware BOM.**

## 9. Questions for Simon before purchasing

1. Can you send photos of **your actual handlebar/stem, front wheel hub and front light**, showing model labels?
2. Does the light only illuminate *while moving*, or does it stay on when stopped? Is there a physical bottle dynamo against the tire?
3. Would you prefer a *visually curved housing with a normal flat LCD*, or is **real curved display glass** a must-have?
4. What is the preferred screen diagonal and whether full street maps are essential for v1 versus turn-by-turn first?

---

**Reference materials:** [Waveshare 4.3-inch display family](https://www.waveshare.com/esp32-s3-touch-lcd-4.3.htm) · [Waveshare 4.3B documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B) · [Hub dynamo electrical FAQ](https://berthoudcycles.fr/nl/faq/faqs/what-loading-current-can-i-expect-from-a-hub-dynamo-for-my-mobile-device)

**Next immediate task:** a verified component shortlist and a real photo/dimensions of Simon's bike. **Nothing purchased. No architecture decision is final until tested.**
