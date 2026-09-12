# EP-7d3: Digital calibration certificate format specification

![image](https://img.shields.io/badge/Status-Draft-blue)

![image](https://img.shields.io/badge/plumCore-0.8.0--dev-gray?labelColor=purple)

## Introduction

This document specifies a format for a digital calibration certificate, a self-contained data object that records
the results of calibrating a device or one of its components against known references. The certificate is encoded
using the Concise Binary Object Representation (CBOR), making it compact, schema-flexible, and suitable for storage
on resource-constrained embedded devices as well as for exchange between systems.

After a calibration is performed, the certificate MAY be signed by the laboratory, the customer, or the responsible
persons to attest to its authenticity and integrity. Signing binds the recorded measurements to the signing party,
allowing any later consumer of the certificate to verify that the data has not been altered and originates from a
trusted source.

A single certificate carries calibration data covering multiple operating conditions, inputs, and environments
(for example a range of applied stimuli measured at several temperatures), and MAY describe more than one
calibration target. A calibration target is the specific entity being characterized — for instance an individual
sensor or an analog-to-digital converter — so that a certificate can describe a complete device along with each of
its independently calibrated parts.

### Key-word usage

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT",
"SHOULD", "SHOULD NOT", "RECOMMENDED", "NOT RECOMMENDED", "MAY", and
"OPTIONAL" in this document are to be interpreted as described in
BCP 14 [RFC2119] [RFC8174] when, and only when, they appear in all
capitals, as shown here.

### License

This work is licensed under CC BY-SA 4.0.
To view a copy of this license, visit https://creativecommons.org/licenses/by-sa/4.0/

© 2026 Marek Koza <qyx@krtko.org>

## Top-level structure

A calibration certificate is a single CBOR indefinite-length map. The map begins with the byte `0xbf` (the CBOR
head for a map of indefinite length) and is terminated by the break byte `0xff`. When the certificate is signed,
the signature bytes follow the terminating `0xff` (see [Signatures](#signatures)).

The first key-value pair of the map MUST be the text string `"CBCAL"` with the unsigned integer value `1`, denoting
the certificate format version. Placing this pair first gives every certificate a fixed, well-known prefix:

```
BF                    # map(*), indefinite length
   65 43 42 43 41 4C  # text(5) "CBCAL"
   01                 # unsigned(1), format version
   ...                # further key-value pairs
FF                    # break, end of map
```

The leading eight bytes therefore form a magic header that uniquely identifies the file as a calibration
certificate:

```
BF 65 43 42 43 41 4C 01
```

Applications MAY use this header to detect a calibration certificate and to determine its format version before
attempting to parse the remaining content. All other key-value pairs follow the version pair in any order, and the
map is closed with the `0xff` break byte.

A minimal certificate carrying no additional key-value pairs is nine bytes long.


## Top-level keys

Besides the mandatory `"CBCAL"` version pair, the following top-level keys are defined. All keys are text strings.
With the exception of the version pair, which MUST come first, the keys MAY appear in any order, and any key MAY be
absent unless stated otherwise.

| Key | Type | Description |
| --- | --- | --- |
| `"CBCAL"` | unsigned int | Format version. MUST be present and MUST be the first pair. Currently `1`. |
| `"adm"` | map | Administrative data describing the certificate, the issuing laboratory and the calibration event. |
| `"tgt"` | map | Calibration targets — the entities that were calibrated (e.g. a sensor, an ADC), keyed by target identifier. |
| `"cfg"` | map | Device configurations referenced by results (e.g. PGA settings), keyed by configuration identifier. |
| `"meq"` | map | Measuring equipment and reference standards referenced by results, keyed by equipment identifier. |
| `"res"` | array | Measurement results, grouped by the influence conditions under which they were obtained. |

### Administrative data `"adm"`

A map carrying metadata *about the certificate* rather than the measured values. It identifies the certificate, the
laboratory that issued it, the customer, the dates of calibration and issue, and the software used to produce it.
Detailed sub-keys are specified in a later section.

### Calibration targets `"tgt"`

A map of maps, one entry per calibrated entity, keyed by the target's identifier. A target describes *what* was
characterized — for example an individual sensor, an analog-to-digital converter, or the device as a whole — together
with identifying information such as a name, serial number, the measurand it measures and the indication it outputs.
Each result in `"res"` refers back to the target it applies to by that identifier, allowing a single certificate to
describe a device and each of its independently calibrated parts. Detailed sub-keys are specified in a later section.

### Configurations `"cfg"`

A map of maps, one entry per discrete device configuration under which calibration was performed, keyed by a
configuration identifier. A configuration is a set of firmware-controlled settings (for example an ADC gain such as
`{ "pga": 1 }`). Results reference the configuration they were taken under by its identifier, so a target calibrated
under several configurations does not have to be replicated. Detailed sub-keys are specified in a later section.

### Measuring equipment `"meq"`

A map of maps, one entry per reference standard or instrument used during calibration, keyed by an equipment
identifier. Each result lists the identifiers of the equipment it was obtained with, so a standard used across many
result groups is described only once. Detailed sub-keys are specified in a later section.

### Measurement results `"res"`

An array of result groups. Each group binds a set of *influence conditions* (the operating point and environment,
such as applied stimulus, temperature, supply voltage) to the *measured quantities* obtained under those conditions.
Shared conditions SHOULD be expressed once at the highest applicable level rather than repeated for every data point.
Every measured quantity is recorded as a value together with its unit, and SHOULD carry an associated measurement
uncertainty. Detailed sub-keys are specified in a later section.

### Example skeleton

The following CBOR diagnostic notation shows the top-level layout with all defined keys present but their contents
omitted:

```
{_
    "CBCAL": 1,
    "adm": { ... },
    "tgt": { ... },
    "cfg": { ... },
    "meq": { ... },
    "res": [ ... ]
}
```

## Administrative data

The `"adm"` value is a map carrying metadata *about the certificate and the calibration event*, as opposed to the
measured values themselves. Its layout is inspired by the `coreData`, `calibrationLaboratory`, `respPersons`,
`customer` and `dccSoftware` parts of the PTB/DKD DCC `administrativeData` element, condensed into compact CBOR keys.
Unlike the DCC, the calibrated objects are not part of the administrative data here; they are described separately in
the top-level `"tgt"` map.

All keys are text strings and, unless noted otherwise, OPTIONAL. Dates and times are encoded as UTF-8 strings in
ISO 8601 format.

| Key | Type | Description |
| --- | --- | --- |
| `"id"` | text | Unique certificate identifier within the issuing laboratory's namespace. SHOULD be present. |
| `"db"` | text | Date begin — date (or date and time) on which the calibration was started. |
| `"de"` | text | Date end — date (or date and time) on which the calibration was completed. |
| `"di"` | text | Date issue — date on which the certificate was issued, i.e. signed and released. |
| `"loc"` | text | Human-readable location at which the calibration was performed. |
| `"lab"` | map | The calibration laboratory issuing the certificate. SHOULD be present. |
| `"cst"` | map | The customer for whom the calibration was performed. |
| `"rsp"` | array | Persons responsible for the calibration. |
| `"sw"` | array | Software used to perform the calibration and produce the certificate. |
| `"rem"` | array | Free-form statements, remarks or declarations (e.g. scope, conformance, comments). |

### Party `"lab"`, `"cst"`

The laboratory (`"lab"`) and customer (`"cst"`) are both encoded as a *party* map sharing a common set of keys:

| Key | Type | Description |
| --- | --- | --- |
| `"n"` | text | Name of the organisation. |
| `"addr"` | text | Postal address as a single, newline-separated string. |
| `"email"` | text | Contact e-mail address. |
| `"phone"` | text | Contact telephone number. |
| `"acc"` | text | Accreditation identifier (e.g. the laboratory's accreditation number). Typically only used for `"lab"`. |
| `"sign"` | text | Signature method this party uses to sign the whole certificate (see [Signatures](#signatures)). |

### Responsible persons `"rsp"`

An array of maps, one per person involved in the calibration:

| Key | Type | Description |
| --- | --- | --- |
| `"n"` | text | Full name of the person. |
| `"role"` | text | Role in the calibration (e.g. `"calibrated by"`, `"approved by"`). |
| `"main"` | bool | If `true`, this person is the principal signatory of the certificate. |
| `"sign"` | text | Signature method this person uses to sign the whole certificate (see [Signatures](#signatures)). |

### Software `"sw"`

An array of maps describing the software used, so that a result can be traced to the tooling that produced it:

| Key | Type | Description |
| --- | --- | --- |
| `"n"` | text | Name of the software component. |
| `"ver"` | text | Version of the software component. |

### Example

```
{_
    "CBCAL": 1,
    "adm": {
        "id": "CAL-2026-000142",
        "db": "2026-06-25T09:00:00Z",
        "de": "2026-06-25T11:30:00Z",
        "di": "2026-06-27",
        "loc": "Metrology Lab 2",
        "lab": {
            "n": "Example Calibration Services s.r.o.",
            "addr": "Example street 1\n123 45 Example City",
            "email": "lab@example.com",
            "acc": "SNAS 0123"
        },
        "cst": {
            "n": "Example Customer Ltd.",
            "addr": "Customer street 7\n678 90 Customer City"
        },
        "rsp": [
            { "n": "Jane Doe", "role": "calibrated by" },
            { "n": "John Roe", "role": "approved by", "main": true }
        ],
        "sw": [
            { "n": "plumcal", "ver": "0.8.0-dev" }
        ],
        "rem": [
            "Calibration performed against references traceable to national standards."
        ]
    },
    "tgt": { ... },
    "res": [ ... ]
}
```

## Calibration targets

The `"tgt"` value is a map of maps, one entry per calibrated entity. The map key is the target's local identifier
(unique within the certificate), and the value is a map describing that target. A *target* describes *what* was
characterized — for example an individual sensor, an analog-to-digital converter, or the device as a whole. This
corresponds to the `item` concept of the PTB/DKD DCC, but is kept at the top level so that results can reference
targets and so that a single certificate can describe a device together with each of its independently calibrated
parts.

Results in the `"res"` array refer back to a target by its identifier (the `"tgt"` map key), and a target MAY refer
to a parent target through `"p"`, forming a hierarchy (e.g. an ADC and a sensor both belonging to one device).

Following the International Vocabulary of Metrology (VIM), a target distinguishes two quantities: the **measurand**
(`"mq"`/`"mu"`) — the physical quantity intended to be measured (VIM 2.3), such as pressure — and the **indication**
(`"iq"`/`"iu"`) — the quantity the target electrically outputs (VIM 4.1), such as resistance. Calibration (VIM 2.39)
establishes the relation between the two, and the compensation model maps the indication back to the measurand.

The following keys are defined within each target map. All keys are text strings and OPTIONAL.

| Key | Type | Description |
| --- | --- | --- |
| `"p"` | text | Parent — identifier (key) of the parent target, if this target is a part of a larger calibrated entity. |
| `"n"` | text | Name — human-readable name or description of the target. |
| `"t"` | text | Type — kind of target, e.g. `"device"`, `"sensor"`, `"adc"`. |
| `"mfg"` | text | Manufacturer of the target. |
| `"pn"` | text | Part number — model designation or part number. |
| `"sn"` | text | Serial number of the target. |
| `"mq"` | text | Measurand — the physical quantity the target measures (VIM 2.3), e.g. `"pressure"`, `"temperature"`. |
| `"mu"` | text | Unit of the measurand. |
| `"iq"` | text | Indication — the quantity the target electrically outputs (VIM 4.1), e.g. `"resistance"`, `"code"`. The compensation model maps the indication `"iq"` to the measurand `"mq"`. |
| `"iu"` | text | Unit of the indication. |

### Example

```
{_
    "CBCAL": 1,
    "adm": { ... },
    "tgt": {
        "dev": {
            "n": "NWDAQ-S2 acquisition module",
            "t": "device",
            "mfg": "Example Instruments",
            "pn": "NWDAQ-S2",
            "sn": "S2-000142"
        },
        "adc0": {
            "p": "dev",
            "n": "Channel 0 ADC",
            "t": "adc",
            "mq": "voltage",
            "mu": "V",
            "iq": "code",
            "iu": "1"
        },
        "rtd0": {
            "p": "dev",
            "n": "Channel 0 RTD temperature sensor",
            "t": "sensor",
            "mq": "temperature",
            "mu": "\\degreecelsius"
        }
    },
    "res": [ ... ]
}
```

## Configurations

Some targets must be calibrated separately under each of several discrete, firmware-controlled operating settings,
because the hardware behaves differently in each. A typical example is an ADC with a programmable-gain amplifier
(PGA): the gain stage has its own offset and gain error, so the channel must be characterized once for every PGA
setting that will be used (PGA = 1, PGA = 2, …).

Rather than replicating the target once per setting, the configurations are collected in the top-level `"cfg"` map,
keyed by a configuration identifier, and each result references the configuration it was taken under (see the result
group `"cfg"` key). A target is therefore listed only once, regardless of how many configurations it is calibrated in.

The `"cfg"` value is a map of maps. The key is the configuration identifier (referenced by results); the value is a
map holding the configuration as a set of setting → value pairs, for example `{ "pga": 1 }`. The setting names and
value types are device-specific.

The configuration is the *runtime selector*: when applying the calibration, the firmware matches its current device
configuration against the `"cfg"` entries and uses the result whose referenced configuration matches. This differs
from `"cond"` (influence conditions, see below), which records the *environment* a measurement was taken in — usually
measured rather than commanded, and not normally used to pick a record.

```
{_
    "CBCAL": 1,
    "cfg": {
        "g1": { "pga": 1 },
        "g2": { "pga": 2 }
    },
    ...
}
```

## Measuring equipment

The top-level `"meq"` map collects the reference standards and instruments used during calibration, so that a
standard shared by many result groups is described only once and referenced by its identifier. Each result lists the
identifiers of the equipment it was obtained with in its `"meq"` array.

The `"meq"` value is a map of maps. The key is the equipment identifier (referenced by results); the value describes
the equipment with the following keys, all text strings:

| Key | Type | Description |
| --- | --- | --- |
| `"n"` | text | Name of the standard or instrument. |
| `"trace"` | text | Traceability reference, e.g. the identifier of the standard's own calibration certificate. |

```
{_
    "CBCAL": 1,
    "meq": {
        "dmm": { "n": "Reference DMM", "trace": "CAL-2025-009921" }
    },
    ...
}
```

## Measurement results

The `"res"` value is an array of *result groups*. Each group fixes a set of influence conditions and records, under
those conditions, a series of measurement points that relate a *reference* quantity (the known value applied or
provided by a standard) to the corresponding quantity *measured* by the target. Several groups capture several
conditions, inputs or environments.

Following the DCC convention, meta-information that is common to all points of a group — the influence conditions,
the measuring equipment and the calibration method — is placed once at the group level rather than repeated per
point. To describe the same target under a genuinely different environment, a new result group with different
`"cond"` values is used.

### Quantity representation

The basic building block of the measurement data is a *quantity*: a value bound to a unit and, where applicable, an
associated measurement uncertainty. A quantity is encoded as a map:

| Key | Type | Description |
| --- | --- | --- |
| `"v"` | number, or array of numbers | The value, or a column of values (one per measurement point). REQUIRED. |
| `"u"` | text | Unit of the value. OPTIONAL — see below. |
| `"unc"` | number, or array of numbers | Uncertainty in the same unit. A scalar applies to every point; an array gives a per-point uncertainty. |
| `"k"` | number | Coverage factor associated with `"unc"` (e.g. `2` for an expanded uncertainty at ~95 %). |

When `"v"` is an array, the quantity represents a *column*. All array-valued quantities within the same result group
have the same length and are aligned by index: index *i* across every column forms one measurement point. This mirrors
the D-SI `si:real` (scalar) and `si:realListXMLList` (vector) constructs of the DCC, reduced to compact CBOR.

`"u"` MAY be omitted when the unit is already known from context: a `"ref"` or `"meas"` column named after the
target's measurand (`"mq"`) or indication (`"iq"`) takes its unit from the target's `"mu"` / `"iu"`. Where the unit
cannot be inferred — notably for influence conditions in `"cond"`, and for any free-standing quantity — `"u"` SHOULD
be given so the quantity stays self-describing. When present, units are encoded as text and the notation SHOULD follow
the D-SI unit notation used by the DCC (e.g. `"V"`, `"\\ohm"`, `"\\degreecelsius"`) so that certificates remain
interoperable with the wider metrology ecosystem; a plain unit symbol is acceptable where D-SI interoperability is
not required.

### Result group

Each entry of `"res"` is a map. `"tgt"` is REQUIRED; the remaining keys are OPTIONAL.

| Key | Type | Description |
| --- | --- | --- |
| `"tgt"` | text | Identifier (key) of the target (from the top-level `"tgt"` map) this group applies to. |
| `"cfg"` | text | Identifier (key) of the configuration (from the top-level `"cfg"` map) the target was in during this group. |
| `"desc"` | text | Human-readable description of the result group. |
| `"cond"` | map | Influence conditions held during this group, as a map of name → quantity (e.g. `temperature`, `supply`). |
| `"ref"` | map | Reference values of the **measurand**, provided by a standard, as a map of name → quantity. Normally array-valued. |
| `"meas"` | map | The **indications** recorded from the target, as a map of name → quantity. Normally array-valued. |
| `"comp"` | map | A fitted compensation model (coefficients) derived from the measurements. See below. |
| `"meq"` | array | Identifiers (keys into the top-level `"meq"` map) of the equipment used to obtain this group. |
| `"method"` | text | Reference to or name of the calibration method or procedure applied. |

A result group provides its data as the raw `"ref"`/`"meas"` tables, as a fitted `"comp"` model, or both. The raw
tables document *what was measured*; the compensation model gives the coefficients a device *applies at runtime*.
Carrying both keeps the certificate self-describing while still being directly usable on the device.

In VIM terms `"ref"` holds reference values of the measurand (from a standard) and `"meas"` holds the corresponding
indications read from the target; calibration relates the two. By convention a column in `"ref"` is named after the
target's measurand (`"mq"`) and a column in `"meas"` after its indication (`"iq"`), so each column's quantity and unit
are those declared on the target. The `"ref"` and `"meas"` columns are aligned row by row and together form the
calibration table for the group.

### Multiple temperatures and drift calibration

Characterizing offset and gain drift requires measuring the same set of points at several temperatures. This is
expressed by emitting **one result group per temperature**, each repeating the same `"ref"`/`"meas"` column structure
but with a different value in `"cond"`. Together the groups form a two-dimensional calibration: the rows within a
group sweep the input (e.g. applied voltage), while the sequence of groups sweeps the influence condition (e.g.
temperature).

There are therefore two distinct ways a device parameter steers record selection:

- a **discrete configuration** — the top-level `"cfg"` map referenced by the result group's `"cfg"` key — where the
  firmware must match the exact record (PGA = 1 vs PGA = 2);
- a **continuous influence condition** — `"cond"` — where the firmware reads the present value (e.g. the current die
  temperature) and *interpolates* between the per-condition result groups to compensate for drift.

Because the influence condition is recorded as a normal quantity inside `"cond"`, the same mechanism extends to any
drift source (supply voltage, reference ageing) and to more than one influence condition at once.

### Compensation coefficients `"comp"`

Instead of, or in addition to, the raw `"ref"`/`"meas"` point tables, a result group MAY carry a *fitted compensation
model* in `"comp"`: the coefficients that map a measured value to a corrected one. This is the form a device usually
applies at runtime, and it avoids shipping (and interpolating) the full point tables on resource-constrained targets.

`"comp"` is a map keyed by *compensation scheme*. Each entry contributes one stage of the correction, and its value
is a map of that scheme's parameters. The indication (input) and measurand (output) quantities, and their units, are
declared in the target definition (`"iq"`/`"iu"` and `"mq"`/`"mu"`), so the schemes carry only coefficients. Two
schemes are defined, `"poly"` and `"tc"`, both mapping directly onto the on-device evaluator implemented by the
`mq-compensation` service so the coefficients can be loaded into a device without transformation.

The correction maps *indication → measurand*: the input is the quantity the target outputs (`"iq"`) and the output is
the measurand the device should report (`"mq"`). The `"poly"` scheme produces the measurand value from the indication;
the `"tc"` scheme, when present, then divides that value by a temperature factor. Each scheme is optional and an
absent scheme is the identity for its stage.

#### `"poly"` — value polynomial

| Key | Type | Description |
| --- | --- | --- |
| `"x0"` | number | Centering offset (reference point) subtracted from the input before evaluation, improving numerical conditioning. Defaults to `0`. |
| `"c"` | array of numbers | Polynomial coefficients in ascending order of exponent: `c[0]` is the offset, `c[1]` the gain, the rest nonlinearity. |

```
value = Σ_i c[i] · (in − x0)^i
```

#### `"tc"` — temperature compensation

| Key | Type | Description |
| --- | --- | --- |
| `"t0"` | number | Temperature reference point subtracted from the temperature before evaluation. Defaults to `0`. |
| `"tc"` | array of numbers | Temperature-compensation coefficients in ascending order, starting at the first order. |

The value is divided by a temperature factor normalised to `1` at the reference temperature `t0`, so the coefficients
express the *relative* deviation per degree:

```
                value
out = ───────────────────────────────────
        1 + Σ_k tc[k] · (T − t0)^(k+1)
```

A device calibrated and operated at `t0` is therefore unaffected by the temperature stage. Combining both schemes
gives the full model:

```
        Σ_i c[i] · (in − x0)^i
out = ───────────────────────────────────
        1 + Σ_k tc[k] · (T − t0)^(k+1)
```

#### Example — polynomial

The PGA = 1 channel, with the raw table replaced by a first-order fit mapping the raw ADC code to a corrected
voltage (`out = c0 + c1·code`):

```
{
    "tgt": "adc0",
    "cfg": "g1",
    "desc": "Voltage compensation, PGA = 1",
    "cond": {
        "temperature": { "v": 25.0, "u": "\\degreecelsius", "unc": 0.3, "k": 2 }
    },
    "comp": {
        "poly": { "c": [-9.1e-4, 7.6294e-5] }
    }
}
```

#### Example — temperature-compensated polynomial

The same channel with a quadratic temperature factor referenced to 25 °C, so one model covers the whole temperature
range (`out = (c0 + c1·code) / (1 + tc1·(T − 25) + tc2·(T − 25)²)`):

```
{
    "tgt": "adc0",
    "cfg": "g1",
    "desc": "Voltage compensation with temperature drift, PGA = 1",
    "comp": {
        "poly": { "x0": 0, "c": [-9.1e-4, 7.6294e-5] },
        "tc": { "t0": 25.0, "tc": [-3.0e-5, 1.2e-7] }
    }
}
```

### Example

The ADC channel calibrated with three applied voltages for each PGA setting (records distinguished by the `"cfg"`
they reference, `g1` and `g2`), with PGA = 1 additionally repeated at 25 °C and 70 °C to capture drift, and the RTD
sensor calibrated against three reference temperatures:

```
{_
    "CBCAL": 1,
    "adm": { ... },
    "tgt": { ... },
    "cfg": {
        "g1": { "pga": 1 },
        "g2": { "pga": 2 }
    },
    "meq": {
        "dmm": { "n": "Reference DMM", "trace": "CAL-2025-009921" }
    },
    "res": [
        {
            "tgt": "adc0",
            "cfg": "g1",
            "desc": "Voltage transfer characteristic, PGA = 1",
            "cond": {
                "temperature": { "v": 25.0, "u": "\\degreecelsius", "unc": 0.3, "k": 2 }
            },
            "ref": {
                "voltage": { "v": [0.0, 2.5, 5.0], "u": "V", "unc": 0.0005, "k": 2 }
            },
            "meas": {
                "code": { "v": [12, 32780, 65510], "u": "1" }
            },
            "meq": ["dmm"]
        },
        {
            "tgt": "adc0",
            "cfg": "g1",
            "desc": "Voltage transfer characteristic, PGA = 1, 70 °C",
            "cond": {
                "temperature": { "v": 70.0, "u": "\\degreecelsius", "unc": 0.3, "k": 2 }
            },
            "ref": {
                "voltage": { "v": [0.0, 2.5, 5.0], "u": "V", "unc": 0.0005, "k": 2 }
            },
            "meas": {
                "code": { "v": [18, 32795, 65528], "u": "1" }
            },
            "meq": ["dmm"]
        },
        {
            "tgt": "adc0",
            "cfg": "g2",
            "desc": "Voltage transfer characteristic, PGA = 2",
            "cond": {
                "temperature": { "v": 25.0, "u": "\\degreecelsius", "unc": 0.3, "k": 2 }
            },
            "ref": {
                "voltage": { "v": [0.0, 1.25, 2.5], "u": "V", "unc": 0.0005, "k": 2 }
            },
            "meas": {
                "code": { "v": [9, 32774, 65500], "u": "1" }
            },
            "meq": ["dmm"]
        },
        {
            "tgt": "rtd0",
            "desc": "Temperature indication",
            "ref": {
                "temperature": { "v": [0.0, 50.0, 100.0], "u": "\\degreecelsius", "unc": 0.05, "k": 2 }
            },
            "meas": {
                "temperature": {
                    "v": [0.12, 49.94, 100.21],
                    "u": "\\degreecelsius",
                    "unc": [0.08, 0.08, 0.10],
                    "k": 2
                }
            }
        }
    ]
}
```

## Signatures

The certificate MAY be signed to attest to its authenticity and integrity. Signing is requested by adding a `"sign"`
key to a *signing entity* in the administrative data — the laboratory (`"lab"`), the customer (`"cst"`), or any
responsible person (an entry of `"rsp"`). The value of `"sign"` is a text string naming the signature method that
entity uses to sign the whole certificate.

A certificate MAY carry more than one signature — for example signed by both the laboratory and the customer, or by
several responsible persons. When multiple entities carry a `"sign"` key, their signatures are appended in the order
the entities appear in the encoded `"adm"` map, with the entries of `"rsp"` taken in array order.

Signatures are appended after the certificate's terminating `0xff` break byte, in the order described above. The exact
on-the-wire format of each signature — what is covered, the encoding, and the length — is defined by its signature
method. The signatures therefore live outside the CBOR map and do not affect parsing of the certificate content. A
verifier reads the `"sign"` keys to learn how many signatures follow, by which method, in what order, and which entity
produced each one; the corresponding public keys are distributed out of band.

### ed25519

> [!WARNING]
> The `ed25519` signature method is not yet established. Its parameters and on-the-wire format are still to be
> specified.

## Minimal certificate

The format is compact enough for severely space-constrained storage. The following is a complete certificate for a
temperature-compensated resistance-to-pressure sensor.

It identifies the sensor and carries a compensation model — a value polynomial (`"poly"`) with temperature
compensation (`"tc"`) — that converts the measured resistance `r` (in ohms), compensated for temperature `t`
(in degrees Celsius), into a pressure `p` (in kilopascals):

```
        50.0 + 0.5·(r − 100)
p = ───────────────────────────
       1 + 1.5e-3·(t − 25)
```

In CBOR diagnostic notation:

```
{_
    "CBCAL": 1,
    "tgt": {
        "ps": { "t": "sensor", "sn": "PS0042", "mq": "pressure", "mu": "kPa", "iq": "resistance", "iu": "\\ohm" }
    },
    "res": [
        {
            "tgt": "ps",
            "comp": {
                "poly": { "x0": 100.0, "c": [50.0, 0.5] },
                "tc": { "t0": 25.0, "tc": [1.5e-3] }
            }
        }
    ]
}
```

The coefficients are encoded as IEEE 754 single-precision (`float32`) values.
