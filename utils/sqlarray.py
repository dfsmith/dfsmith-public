#!/usr/bin/env python3
# Daniel F. Smith, 2020

import os
import errno
import re
import sqlite3
from collections import UserDict

class SQLArray(UserDict):
    """
    A class that makes a SQLite3 database into an array-like object.
    """
    def __init__(self, sqlfilename, create=True, convert=None, unconvert=None):
        """
        Open or create a SQLite3 database and return an array-like object
        of tables in the database.  Each table is a (key, value) store.
        create: bool
            Create the DB if it does not exist.
        convert: function(object) -> str
            When writing a value, convert it to a string with this function.
            E.g., json.dumps.
        unconvert: function(str) -> object
            When reading a value, convert it from a string with this function.
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
            self.cursor = self.db.sql.execute("select name from sqlite_master where type='table'")

        def __next__(self):
            tablename = self.cursor.__next__()[0]
            return self.db.Table(db, tablename)

    def __iter__(self):
        return self.IterTables(self)

    def __getitem__(self, tablename):
        return self.Table(self, tablename)

    class Table():
        def __init__(self, db, name):
            self.db = db
            if not db.allowedtablename(name):
                raise KeyError
            self.name = name
            self.db.sql.execute("create table if not exists %s (key PRIMARY KEY, value)" % self.name)

        def __repr__(self):
            return self.db.__repr__() + "['%s']" % self.name

        def __str__(self):
            return self.name

        def row(self, key):
            return self.db.sql.execute("select value from %s where key=?" % self.name,
                                    (key,)).fetchone()

        def __len__(self):
            return self.db.sql.execute("select count(*) from %s" % self.name).fetchone()[0]

        def __getitem__(self, key):
            row = self.row(key)
            if not row:
                raise KeyError
            return self.db.unconvert(row[0])
        
        def __setitem__(self, key, value):
            v = self.db.convert(value)
            if self.row(key):
                self.db.sql.execute("update %s set value=? where key=?" % self.name,
                                (v, key))
            else:
                self.db.sql.execute("insert into %s (key,value) values (?,?)" % self.name,
                                (key,v))
            self.db.sql.commit()
        
        def __delitem__(self, key):
            if not self.row(key):
                raise KeyError
            self.db.sql.execute("delete from %s where key=?" % self.name,
                                (key,))

        def iterate(self):
            c = self.db.sql.cursor()
            return c.execute("select key from %s" % self.name)

        def list(self):
            return [row[0] for row in self.iterate().fetchall()]

        class IterKey():
            def __init__(self, cursor):
                self.cursor = cursor

            def __next__(self):
                return self.cursor.__next__()[0]

        def __iter__(self):
            return self.IterKey(self.iterate())

if __name__ == "__main__":
    import sys
    for file in sys.argv[1:]:
        db = SQLArray(file, False)
        for table in db:
            for key in table:
                print("%s['%s']['%s']: %s" % (file, table, key, table[key]))

#    db=SQLArray("wibble")
#    db["secondary"]["really"]="phreooooow!"
#    db["another"]["yokeydokey"]="uh-huh-huh"
#
#    import json
#    db=SQLArray("spong", convert=json.dumps, unconvert=json.loads)
#    a=db['sqlarray']
#    d={}
#    d['one']="1"
#    d['two']="2"
#    a['object']=d
