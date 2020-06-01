#!/usr/bin/env python3
# (C) Daniel F. Smith, 2020
# SPDX-License-Identifier: LGPL-2.1
# Other licenses on request.

import os
import errno
import re
import sqlite3
from collections import UserDict

class SQLArray(UserDict):
    """
    A class that makes a SQLite3 database into an array-like object.

    Each key at the top level is the name of a database table.  Queries
    can be run against tables.  For example:

    from sqlarray import SQLArray
    table = SQLArray("my_filename")["mytable"]
    table["hello"] = "world"
    for key in table.search("or"):
        print(table[key])
    """

    def __init__(self, sqlfilename, create=True, convert=None, unconvert=None):
        """
        Open or create a SQLite3 database and return an array-like object
        of tables in the database.  Each subsequent table is a (key, value) store.

        Iterating over an SQLArray returns the tables.

        sqlfilename: string
            A filename to use as the SQLite database.  This will have '_sa.sqlite'
            appended to it if the file does not already exist.
        create: bool
            Create the DB if it does not exist.
        convert: function(object) -> str
            When writing a value to the database, it will be converted to a string
            with this function.
            E.g., json.dumps.
        unconvert: function(str) -> object
            When reading a value from the database, it will be converted from a
            string with this function.
            E.g., json.loads.
        """
        self.allowedtablename = re.compile("[a-zA-Z_]+").search
        self.filename = sqlfilename
        self.convert = convert if convert else self.raw
        self.unconvert = unconvert if unconvert else self.raw

        if not os.path.isfile(self.filename):
            self.filename = "%s_sa.sqlite" % sqlfilename
        if not create and not os.path.isfile(self.filename):
            raise FileNotFoundError(errno.ENOENT, self.filename)
        #print("connecting to %s" % self.filename)
        self.sql = sqlite3.connect(self.filename)

    def raw(self, string):
        return string

    def __repr__(self):
        return "SQLArray('%s')" % self.filename

    class IterTables():
        def __init__(self, db):
            self.db = db
            self.cursor = self.db.sql.execute("SELECT name FROM sqlite_master WHERE type='table'")

        def __next__(self):
            tablename = self.cursor.__next__()[0]
            return self.db.Table(db, tablename)

    def __iter__(self):
        return self.IterTables(self)

    def __getitem__(self, tablename):
        return self.Table(self, tablename)
    
    def vacuum(self):
        """Compact the database by removing holes (deletions)."""
        self.sql.execute("VACUUM")

    class Table():
        """An associative array based on a table in an SQLite backed database."""

        def __init__(self, db, name, convert=None, unconvert=None):
            self.db = db
            self.convert = convert if convert else db.convert
            self.unconvert = unconvert if unconvert else db.unconvert
            if not db.allowedtablename(name):
                raise KeyError
            self.name = name
            self.db.sql.execute("CREATE TABLE IF NOT EXISTS %s (key PRIMARY KEY, value)" % self.name)

        def __repr__(self):
            return self.db.__repr__() + "['%s']" % self.name

        def __str__(self):
            return self.name

        def row(self, key):
            return self.db.sql.execute("SELECT value FROM %s WHERE key=?" % self.name,
                                    (key,)).fetchone()

        def __len__(self):
            return self.db.sql.execute("SELECT count(*) FROM %s" % self.name).fetchone()[0]

        def __getitem__(self, key):
            row = self.row(key)
            if not row:
                raise KeyError
            return self.unconvert(row[0])
        
        def __setitem__(self, key, value):
            v = self.convert(value)
            self.db.sql.execute("REPLACE INTO %s (key,value) VALUES (?,?)" % self.name,
                                (key,v))
            self.db.sql.commit()
        
        def __delitem__(self, key):
            if not self.row(key):
                raise KeyError
            self.db.sql.execute("DELETE FROM %s WHERE key=?" % self.name,
                                (key,))

        class IterKey():
            def __init__(self, cursor):
                self.cursor = cursor

            def __iter__(self):
                return self

            def __next__(self):
                return self.cursor.__next__()[0]

        def __iter__(self):
            c = self.db.sql.cursor()
            return self.IterKey(c.execute("SELECT key FROM %s" % self.name))

        def list(self):
            """Return a list of all keys."""
            return [row for row in self]

        def like(self, search):
            """Case insensitive SQL-like search over raw values."""
            c = self.db.sql.cursor()
            c.execute("SELECT key FROM %s WHERE value LIKE ? ESCAPE '\\'" % self.name,
                        (search,))
            return self.IterKey(c)

        def glob(self, search):
            """Case sensitive glob-like search over raw values."""
            c = self.db.sql.cursor()
            c.execute("SELECT key FROM %s WHERE value GLOB ?" % self.name,
                        (search,))
            return self.IterKey(c)

        def search(self, search):
            """Search for sub-string over raw values.  (Uses glob.)"""
            search.replace("*", "\\*").replace("?", "\\?").replace("[", "\\[").replace("]", "\\]")
            return self.glob("*" + search + "*")

if __name__ == "__main__":
    import sys
    for file in sys.argv[1:]:
        db = SQLArray(file, False)
        for table in db:
            for key in table:
                print("%s['%s']['%s']: %s" % (file, table, key, table[key]))

#    import time
#    sadb = SQLArray("spong")["sqlarray"]
#    sadb[time.time()] = time.ctime()

#    db = SQLArray("wibble")
#    db["secondary"]["really"] = "phreooooow!"
#    db["another"]["yokeydokey"] = "uh-huh-huh"

#    import json
#    db = SQLArray("spong", convert=json.dumps, unconvert=json.loads)
#    array = db['sqlarray']
#    d = {}
#    d['one'] = "1"
#    d['two'] = "2"
#    array['object'] = d
