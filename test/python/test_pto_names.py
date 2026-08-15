"""An object name is a path, and a name is not an identity.

Two defects fixed together, because they are the same
question asked twice: *what does a container's object name mean?*

* **It means a relative path** — `disassemble` puts the container back as a
  directory tree and the name carries the layout. So a name is an instruction
  to write somewhere, and one that points outside the target directory was
  followed. An object named ``../victim`` overwrote a file outside the
  directory and the call returned success.
* **It does not mean an identity** — several objects may share one name, on
  purpose, because re-running an analysis keeps the older result reachable.
  `find` resolved that tie by returning the *oldest*, so the most obvious call
  handed back the stalest analysis in the container.

The traversal tests are the interesting half. A `.pto` is an interchange
format — being passed between people is the point — so `disassemble` is what a
recipient runs on a file somebody else wrote, and the writer's own refusal
proves nothing about a hostile file. The reader is tested against a container
that is byte-patched after writing, because the writer will no longer produce
one.
"""
import numpy as np
import pytest

import tttrlib


def _store(n=1):
    s = tttrlib.DataStore("t")
    s.set_n_rows(n)
    s.add("x", np.zeros(n))
    return s


def _write(path, names, title="names"):
    f = tttrlib.PtoFile()
    assert f.create(str(path), title), f.error()
    uids = [tttrlib.pto_add_store(f, "burst_table", n, _store()) for n in names]
    assert f.commit(), f.error()
    f.close()
    return uids


class TestANameThatWouldEscapeIsRefusedOnTheWayIn:
    """The writer will not produce a container nobody can safely unpack."""

    # Each of these reaches, or could reach, outside the target directory.
    @pytest.mark.parametrize("name", [
        "../escape",            # the filed reproduction
        "a/../../escape",       # survives normalisation
        "/abs/rooted",          # absolute
        "..",                   # nothing but the traversal
        ".",                    # normalises to no file at all
    ])
    def test_a_traversing_name_is_rejected(self, tmp_path, name):
        f = tttrlib.PtoFile()
        assert f.create(str(tmp_path / "w.pto"), "reject")
        assert tttrlib.pto_add_store(f, "burst_table", name, _store()) == 0
        # The refusal names the object and the reason, rather than failing bare.
        assert name in f.error()

    @pytest.mark.parametrize("name", ["..\\win\\escape", "C:\\drive"])
    def test_a_windows_shaped_escape_is_rejected_on_every_platform(self, tmp_path, name):
        """`\\` is an ordinary filename character on POSIX and a separator on
        Windows. A container written on one is unpacked on the other, so a name
        that only traverses on Windows must still be refused here — otherwise
        the check is absent exactly where the file crosses machines."""
        f = tttrlib.PtoFile()
        assert f.create(str(tmp_path / "w.pto"), "reject")
        assert tttrlib.pto_add_store(f, "burst_table", name, _store()) == 0

    @pytest.mark.parametrize("name", [
        "plain",
        "one/two",
        "a/b/c/d/deep",
        "countrate_All 0.2000#30/bursts",   # what a real burst run writes
        "a/../b",                           # normalises to "b", stays inside
        "./x",
    ])
    def test_a_name_that_stays_inside_is_accepted(self, tmp_path, name):
        """The gate must not cost the feature it protects. A name containing a
        separator is *supported* — ChiSurf addresses a container like a folder
        on the strength of it."""
        f = tttrlib.PtoFile()
        assert f.create(str(tmp_path / "w.pto"), "accept")
        assert tttrlib.pto_add_store(f, "burst_table", name, _store()) != 0, f.error()


class TestAHostileContainerIsRefusedOnTheWayOut:
    """The half that matters: the writer's gate says nothing about a file this
    library did not write, and that is the whole threat."""

    @staticmethod
    def _hostile(path, safe_name, evil_name):
        """A container carrying `evil_name`, built by patching the bytes.

        The writer now refuses such a name, so the only way to get one is the
        way a real attacker would: another tool. Same byte length in and out,
        so every offset in the container stays valid.
        """
        assert len(safe_name) == len(evil_name)
        _write(path, [safe_name])
        raw = path.read_bytes()
        assert safe_name.encode() in raw
        path.write_bytes(raw.replace(safe_name.encode(), evil_name.encode()))

    def test_it_does_not_write_outside_the_directory_it_is_given(self, tmp_path):
        victim = tmp_path / "victim"
        victim.mkdir()
        keep = victim / "keep.txt"
        keep.write_text("original contents\n")

        p = tmp_path / "hostile.pto"
        self._hostile(p, "aa/victim/keep.txt", "../victim/keep.txt")

        g = tttrlib.PtoFile()
        assert g.open(str(p))
        # The name really is in the container -- the patch took.
        assert [o.name for o in g.objects()] == ["../victim/keep.txt"]

        assert list(g.disassemble(str(tmp_path / "unpack"))) == []
        # Read as text: the failure this replaces overwrote the file with
        # dstore binary, which does not decode as UTF-8.
        assert keep.read_text() == "original contents\n"

    def test_it_refuses_the_whole_call_rather_than_skipping_the_object(self, tmp_path):
        """Unpacking the safe objects and quietly dropping the rest leaves a
        directory that looks complete and is not."""
        p = tmp_path / "hostile.pto"
        _write(p, ["good_one", "aa/evil", "good_two"])
        raw = p.read_bytes()
        p.write_bytes(raw.replace(b"aa/evil", b"../evil"))

        out = tmp_path / "unpack"
        g = tttrlib.PtoFile()
        assert g.open(str(p))
        assert list(g.disassemble(str(out))) == []
        assert not out.exists()
        assert not (tmp_path / "evil").exists()

    def test_the_refusal_says_which_object_and_why(self, tmp_path):
        p = tmp_path / "hostile.pto"
        self._hostile(p, "aa/escape", "../escape")
        g = tttrlib.PtoFile()
        g.open(str(p))
        g.disassemble(str(tmp_path / "unpack"))
        err = g.error()
        assert "../escape" in err and ".." in err

    def test_a_legitimate_layout_still_round_trips(self, tmp_path):
        """The original defect — the one whose fix opened the traversal hole.
        Names with separators must still create their directories."""
        p = tmp_path / "m000.pto"
        names = ["countrate_All 0.2000#30/bursts", "a/b/c/d/deep", "plain"]
        _write(p, names)

        out = tmp_path / "unpack"
        g = tttrlib.PtoFile()
        g.open(str(p))
        written = list(g.disassemble(str(out)))

        assert len(written) == len(names)
        for n in names:
            assert (out / n).is_file(), f"{n} was not written"


class TestTheCommandLineUnpacksThroughTheSameGate:
    """`tttr pto extract FILE DIR` is how a recipient actually unpacks a
    container they were sent, and it had its own copy of the loop — kept only
    so its progress count matched the object list, and missing the check as a
    direct result. It now delegates, so there is one implementation to get
    right. These run the built binary because the defect was in the CLI and
    not in the library it links."""

    @staticmethod
    def _tttr():
        import os
        import shutil
        from pathlib import Path
        # An explicit override wins (same env var as the CLI tests in
        # test/misc): a build tree outside build/ -- or a PATH tttr that is
        # stale or broken -- must not decide which binary is tested.
        override = os.environ.get("TTTRLIB_CLI")
        if override:
            return override
        # Every build tree is under build/ -- see the placement check at the top
        # of CMakeLists.txt -- so glob it rather than naming one.
        root = Path(__file__).resolve().parents[2]
        for c in sorted(root.glob("build/*/bin/tttr")) + [
                Path(shutil.which("tttr") or "/nonexistent")]:
            if c.is_file():
                return str(c)
        pytest.skip("the tttr CLI is not built")

    def test_it_refuses_a_container_that_would_write_outside_dir(self, tmp_path):
        import subprocess
        exe = self._tttr()

        p = tmp_path / "hostile.pto"
        _write(p, ["aa/pwned.txt"])
        p.write_bytes(p.read_bytes().replace(b"aa/pwned.txt", b"../pwned.txt"))

        work = tmp_path / "work"
        work.mkdir()
        r = subprocess.run([exe, "pto", "extract", str(p), "unpack"],
                           cwd=work, capture_output=True, text=True)

        assert r.returncode == 1, r.stdout + r.stderr
        assert not (work / "pwned.txt").exists()
        assert not (tmp_path / "pwned.txt").exists()
        assert ".." in (r.stderr + r.stdout)

    def test_a_legitimate_container_still_extracts(self, tmp_path):
        """Including the UID prefix two objects sharing a name get — the
        behaviour the duplicated loop existed to reproduce."""
        import subprocess
        exe = self._tttr()

        p = tmp_path / "m000.pto"
        _write(p, ["bursts", "bursts", "nested/deep/thing"])

        out = tmp_path / "out"
        out.mkdir()
        r = subprocess.run([exe, "pto", "extract", str(p), str(out)],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stdout + r.stderr

        assert (out / "bursts").is_file()
        assert (out / "nested" / "deep" / "thing").is_file()
        # the second `bursts` keeps its payload under a uid-prefixed name
        assert any(f.name.endswith("-bursts") for f in out.iterdir())


class TestANameIsNotAnIdentity:
    """Three runs of one analysis write three objects called `bursts`."""

    NAMES = ["bursts", "bursts", "bursts"]

    def test_objects_come_back_in_write_order(self, tmp_path):
        """The contract every "newest wins" reader leans on. It held before the
        fix but was not promised, which is what made leaning on it a guess."""
        p = tmp_path / "m000.pto"
        uids = _write(p, self.NAMES)
        g = tttrlib.PtoFile()
        g.open(str(p))
        assert [o.uid for o in g.objects()] == uids

    def test_find_returns_the_newest_not_the_oldest(self, tmp_path):
        """The defect: `find` returned the first match, so a reader using the
        most natural call got the stalest analysis in the container — with no
        sign that anything newer existed."""
        p = tmp_path / "m000.pto"
        uids = _write(p, self.NAMES)
        g = tttrlib.PtoFile()
        g.open(str(p))
        assert g.find("bursts") == uids[-1]
        assert g.find("bursts") != uids[0]

    def test_find_all_shows_every_one_oldest_first(self, tmp_path):
        """So a reader can notice there is more than one at all, instead of
        re-deriving newest-wins from `objects()` — which is how two readers
        come to disagree about which result a container is showing."""
        p = tmp_path / "m000.pto"
        uids = _write(p, self.NAMES)
        g = tttrlib.PtoFile()
        g.open(str(p))
        assert list(g.find_all("bursts")) == uids

    def test_a_name_nothing_carries_is_empty_not_an_error(self, tmp_path):
        p = tmp_path / "m000.pto"
        _write(p, self.NAMES)
        g = tttrlib.PtoFile()
        g.open(str(p))
        assert g.find("no_such_object") == 0
        assert list(g.find_all("no_such_object")) == []

    def test_the_object_find_returns_is_the_last_one_written(self, tmp_path):
        """Identity by content, not just by uid: the three stores differ in row
        count, so this fails if `find` resolves to the wrong run even when the
        uids happen to line up."""
        p = tmp_path / "m000.pto"
        f = tttrlib.PtoFile()
        f.create(str(p), "rows")
        for n in (4621, 2318, 1099):
            tttrlib.pto_add_store(f, "burst_table", "bursts", _store(n))
        f.commit()
        f.close()

        g = tttrlib.PtoFile()
        g.open(str(p))
        found = [o for o in g.objects() if o.uid == g.find("bursts")][0]
        assert found.rows == 1099
