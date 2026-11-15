"""Exercise rollback and conflict checks without touching the user's desktop."""
import importlib.machinery
import importlib.util
from pathlib import Path
from unittest import TestCase, main, mock
import tempfile

loader = importlib.machinery.SourceFileLoader('hotkeys', str(Path(__file__).resolve().parents[1] / 'bin/micpad-hotkeys'))
spec = importlib.util.spec_from_loader(loader.name, loader)
hotkeys = importlib.util.module_from_spec(spec)
loader.exec_module(hotkeys)


class HotkeysTest(TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.path = self.root / '.config/hypr/bindings.lua'
        self.path.parent.mkdir(parents=True)
        self.original = '-- personal keybindings\no.bind("CTRL + INSERT", "Mute", "my-mute")\n'
        self.path.write_text(self.original)

    def tearDown(self):
        self.directory.cleanup()

    def run_installer(self, replies):
        with mock.patch.object(hotkeys.Path, 'home', return_value=self.root), \
             mock.patch.object(hotkeys.sys, 'argv', ['micpad-hotkeys']), \
             mock.patch.object(hotkeys, 'call', side_effect=replies):
            hotkeys.main()

    def test_preserves_existing_bindings_and_is_idempotent(self):
        self.run_installer(['', '[]', 'ok', ''])
        updated = self.path.read_text()
        self.assertIn(self.original.rstrip(), updated)
        self.assertEqual(updated.count('Micpad sound'), 9)
        self.assertEqual(len(list(self.path.parent.glob('*.bak.micpad-*'))), 1)
        self.run_installer([])
        self.assertEqual(self.path.read_text(), updated)

    def test_conflict_does_not_write(self):
        with self.assertRaises(RuntimeError):
            self.run_installer(['', '[{"key":"1","modmask":12}]'])
        self.assertEqual(self.path.read_text(), self.original)

    def test_config_failure_restores_original(self):
        with self.assertRaises(RuntimeError):
            self.run_installer(['', '[]', 'ok', 'bad config', 'ok'])
        self.assertEqual(self.path.read_text(), self.original)

    def test_unavailable_compositor_does_not_write(self):
        with self.assertRaises(RuntimeError):
            self.run_installer([RuntimeError('Cannot connect')])
        self.assertEqual(self.path.read_text(), self.original)


if __name__ == '__main__':
    main()
