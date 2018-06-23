# pretix-aqbanking

This tool serves the same purpose pretix-banktool does, however it uses the widely used aqbanking API, packaged in several distributions.

### Installation

Install aqbanking and compile pretix-aqbanking using either gcc or clang:

```
 clang main.c -I /usr/include/aqbanking5 -I /usr/include/gwenhywfar4 -lcurl -lgwenhywfar -laqbanking -Wall -Wpedantic -Wextra -o pretix-aqbanking
```

### Usage

Just configure your aqbanking account (for example using aqbanking-cli, or other programs like gnucash) and look for your account:

```
pretix-aqbanking --list
```

Afterwards you can test it by printing all available transactions (wildcards are possible, so if you only got one account you can use "*"):

```
pretix-aqbanking --list_transactions "<account number>"
```

Then you can simply send all transactions to the pretix instance, you only need to add the event name, pretix url and pretix token.

```
pretix-aqbanking --send_transactions "<account number>" "<event name>" "<pretix url, e.g. https://pretix.eu/api/v1/organizers/{event_name}/bankimportjobs/>" "<pretix token>"
```

