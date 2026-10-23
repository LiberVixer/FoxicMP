"""Pure ownership/parser tests; no access to the user's audio server."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('audio_output', Path(__file__).resolve().parents[1] / 'helpers/audio_output.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class HelperTest(unittest.TestCase):
    def test_names_cannot_inject_module_options_or_create_cycles(self):
        self.assertEqual(helper.device_name('pulse/alsa_output.usb-test:1'), 'alsa_output.usb-test:1')
        for name in ['alsa/default', 'pulse/', 'pulse/a,b', 'pulse/a b', 'pulse/a\nslaves=x', 'pulse/foxicmp_dual_123_abc']:
            with self.assertRaises(ValueError):
                helper.device_name(name)

    def test_cleanup_checks_ownership_after_server_reuses_module_id(self):
        route = helper.Route()
        route.module = 31
        with patch.object(helper, 'items', return_value=[{'index': 31, 'name': 'module-null-sink', 'argument': 'sink_name=someone_else'}]), patch.object(helper, 'pactl') as command:
            route.clear()
            command.assert_not_called()
        route.module = 31
        with patch.object(helper, 'items', return_value=[{'index': 31, 'name': 'module-combine-sink', 'argument': 'sink_name=' + route.name}]), patch.object(helper, 'pactl') as command:
            route.clear()
            command.assert_called_once_with('unload-module', '31')

    def test_duplicate_or_missing_sink_never_loads_module(self):
        for secondary in ['pulse/one', 'pulse/missing']:
            route = helper.Route()
            with patch.object(helper, 'items', return_value=[{'name': 'one'}, {'name': 'two'}]), patch.object(helper, 'pactl') as command:
                with self.assertRaises(RuntimeError):
                    route.update('pulse/one', secondary)
                command.assert_not_called()

    def test_default_private_route_is_not_chained_and_recovery_reloads(self):
        route = helper.Route()
        route.pair = ('one', 'two')
        route.healthy = True
        sinks = [{'name': 'one'}, {'name': 'two'}, {'name': route.name}]
        with patch.object(helper, 'items', return_value=sinks), patch.object(helper, 'pactl', return_value=route.name) as command:
            route.update('auto', 'pulse/two')
            command.assert_called_once_with('get-default-sink')
        route.healthy = False
        with patch.object(helper, 'items', return_value=sinks), patch.object(route, 'clear') as clear, patch.object(helper, 'pactl', return_value='99') as command:
            route.update('pulse/one', 'pulse/two')
            clear.assert_called_once()
            self.assertEqual(command.call_args.args[0:2], ('load-module', 'module-combine-sink'))
            self.assertEqual(route.module, 99)


if __name__ == '__main__':
    unittest.main()
