import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('verify', Path(__file__).parents[1] / 'verify_factory_presets.py')
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)

class PresetEvidenceTests(unittest.TestCase):
    def test_valid_inline_id_has_independent_declaration(self):
        row = verify.analyze('make_unique<AudioParameterFloat>("gain", "Gain", 0, 1, 0); bank.addFactoryPreset("A", {{"gain", 0.5f}});')
        self.assertEqual(row['status'], 'literal-evidence')
        self.assertEqual(row['missing_literal_ids'], [])

    def test_unique_bogus_inline_id_cannot_validate_itself(self):
        row = verify.analyze('bank.addFactoryPreset("A", {{"uniqueBogusParameter", 0.5f}});')
        self.assertEqual(row['status'], 'missing-literal-evidence')
        self.assertEqual(row['missing_literal_ids'], ['uniqueBogusParameter'])

    def test_comments_and_other_preset_calls_are_not_evidence(self):
        row = verify.analyze('// "bogus"\nbank.addFactoryPreset("(A)", {{"bogus", 0.5f}}); bank.addFactoryPreset("B", {{"bogus", 1.0f}});')
        self.assertEqual(row['missing_literal_ids'], ['bogus'])
        self.assertEqual(row['preset_calls'], 2)

    def test_method_declaration_is_not_a_preset_call(self):
        row = verify.analyze('void addFactoryPreset(const String& name, PresetValues values);')
        self.assertEqual(row['status'], 'not-applicable')

    def test_parameter_id_braced_declaration_remains_evidence(self):
        row = verify.analyze('juce::ParameterID { "gain", 1 }; bank.addFactoryPreset("A", {{"gain", 0.5f}});')
        self.assertEqual(row['status'], 'literal-evidence')

    def test_generated_id_requires_review(self):
        row = verify.analyze('juce::ParameterID(prefix + "gain", 1); bank.addFactoryPreset("A", {{"slot1gain", 0.5f}});')
        self.assertEqual(row['status'], 'review')
        self.assertTrue(row['generated_parameter_ids'])

    def test_indirect_preset_requires_review(self):
        row = verify.analyze('PresetValues values {{"bogus", 0.5f}}; bank.addFactoryPreset("A", values);')
        self.assertEqual(row['status'], 'review')
        self.assertEqual(row['indirect_or_unparsed_calls'], 1)

    def test_named_list_cannot_validate_separate_inline_typo(self):
        row = verify.analyze('PresetValues values {{"bogus", 0.5f}}; bank.addFactoryPreset("A", {{"bogus", 1.0f}});')
        self.assertEqual(row['status'], 'missing-literal-evidence')

if __name__ == '__main__':
    unittest.main()
