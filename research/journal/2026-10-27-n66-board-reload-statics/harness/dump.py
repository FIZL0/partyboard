# gdb: `pbdump <out> <tag>` appends every static of src/game/board/*.c (list: $PB_SYMS, "file name size" lines)
# as "tag file::name = value", values printed by gdb (pointers kept; diff.py masks heap addresses).
import gdb, os

class PbDump(gdb.Command):
    def __init__(self):
        super().__init__("pbdump", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        out, tag = arg.split()
        gdb.execute("set print elements unlimited", to_string=True)
        gdb.execute("set print repeats unlimited", to_string=True)
        gdb.execute("set print pretty off", to_string=True)
        with open(os.environ["PB_SYMS"]) as f, open(out, "a") as o:
            for line in f:
                fn, name, size = line.split()
                try:
                    v = gdb.parse_and_eval("'%s'::%s" % (fn, name))
                    s = str(v).replace("\n", " ")
                except gdb.error as e:
                    s = "<err %s>" % e
                o.write("%s %s::%s = %s\n" % (tag, fn, name, s))

PbDump()
