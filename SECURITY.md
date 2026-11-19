# Security policy

## Reporting a vulnerability

Do not open a public issue for suspected vulnerabilities. Use GitHub's private
vulnerability reporting for `adrighem/purple-xmpp2`. If that is unavailable,
contact the maintainer through the private address in the GitHub profile and
include `purple-xmpp2 security` in the subject.

Include affected revisions, impact, and reproduction steps. Do not send
account passwords, tokens, or private message contents.

## Supported versions

Security fixes target the latest release and the main development branch.
Older versions do not receive backports.

## Scope

purple-xmpp2 is a fork of Pidgin's jabber plugin, so it inherits that code's
stream, authentication, and TLS handling (through libpurple). Report issues in
code that this project added or changed here. Report flaws that exist in
unmodified upstream code to the [Pidgin project](https://pidgin.im/) as well.
The project has not undergone an independent audit.
