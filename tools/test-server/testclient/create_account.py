#!/usr/bin/env python3
"""Creates (or resets the password of) an AzerothCore account straight in acore_auth.

Lets setup.sh make accounts without a running worldserver console.
Example: python3 create_account.py admin secret --gm 3
"""

import argparse
import os
import subprocess
import sys

from wowclient import srp6_verifier


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("username")
    ap.add_argument("password")
    ap.add_argument("--gm", type=int, default=0, help="GM level (0 = player, 3 = admin)")
    ap.add_argument("--db-user", default="acore")
    ap.add_argument("--db-pass", default="acore")
    ap.add_argument("--db-name", default="acore_auth")
    args = ap.parse_args()

    username = args.username.upper()
    if len(username) > 16 or len(args.password) > 16:
        sys.exit("username and password must be 16 characters or fewer (client limit)")

    salt = os.urandom(32)
    verifier = srp6_verifier(username, args.password, salt)
    sql = (
        "INSERT INTO account (username, salt, verifier, expansion) VALUES ('{u}', 0x{s}, 0x{v}, 2) "
        "ON DUPLICATE KEY UPDATE salt=VALUES(salt), verifier=VALUES(verifier);"
        "DELETE FROM account_access WHERE id=(SELECT id FROM account WHERE username='{u}');"
    ).format(u=username, s=salt.hex(), v=verifier.hex())
    if args.gm > 0:
        sql += "INSERT INTO account_access (id, gmlevel, RealmID) SELECT id, {gm}, -1 FROM account WHERE username='{u}';".format(
            gm=args.gm, u=username)

    out = subprocess.run(["mysql", "-u" + args.db_user, "-p" + args.db_pass, args.db_name, "-e", sql],
                         capture_output=True, text=True)
    if out.returncode != 0:
        errors = [line for line in out.stderr.splitlines() if "Using a password" not in line]
        sys.exit("mysql failed: " + "\n".join(errors))
    print("account %s ready%s" % (username, " (GM level %d)" % args.gm if args.gm else ""))


if __name__ == "__main__":
    main()
