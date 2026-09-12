# Enhancement proposals

Enhancement proposals (EPs) are design documents used to standardize things before they are implemented. Rather than
growing a protocol or a service organically in code and reverse-engineering its rules afterwards, an EP captures the
intended data formats, wire protocols, interfaces and behaviour up front, so the design can be reviewed and agreed upon
while it is still cheap to change.

Once the corresponding functionality is implemented, the EP does not become obsolete. It lives on as the conceptual
and normative reference for that piece of the system: it explains the reasoning and design goals behind a protocol or
service implementation, and it defines what a conforming implementation must do. When the code and the EP disagree,
the EP is the specification the code is expected to follow.


## Structure of an enhancement proposal

Every EP follows the same skeleton so proposals stay easy to read and compare against each other. An EP should contain,
in order:

- **Status** — one of *draft*, *active* or *obsolete*. A *draft* is still being worked out and may change at any time;
  an
  *active* EP describes a design that has been agreed upon and is usually implemented; an *obsolete* EP has been
  superseded or abandoned and is kept only for reference.
- **plumCore version** — the plumCore version in which the EP was first introduced, so a reader can tell how long the
  proposal has been around and against which baseline it was written.
- **Introduction** — a short summary of what the EP proposes and why, stating the problem it solves and its design
  goals.
- **Key-word usage** — a note clarifying that the requirement key words ("MUST", "SHOULD", "MAY", ...) are to be
  interpreted as described in BCP 14 \[RFC2119\] \[RFC8174\] when, and only when, they appear in all capitals.
- **License** — EPs are licensed under CC BY-SA 4.0 (<https://creativecommons.org/licenses/by-sa/4.0/>), followed by the
  copyright line.
- **EP content** — the normative body of the proposal: the data formats, wire protocols, interfaces and behaviour being
  standardized.
- **Examples** — one or more worked examples at the end, illustrating the format or protocol described above.

The status and the plumCore version are conventionally rendered as badges at the top of the page, directly below the
title.

EPs are written in Markdown and word-wrapped at 120 characters.
