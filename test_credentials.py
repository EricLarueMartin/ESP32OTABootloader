import hashlib, json, tempfile, unittest
from pathlib import Path
from credentials import create

class Credentials(unittest.TestCase):
    def test_unique_and_non_overwriting(self):
        with tempfile.TemporaryDirectory() as folder:
            d=Path(folder)
            a=json.loads(create(d,"bedroom-side-blinds").read_text())
            b=json.loads(create(d,"huzzah32-test").read_text())
            self.assertEqual(len(a["key"]),64)
            self.assertEqual(a["auth_sha256"],hashlib.sha256(a["key"].encode()).hexdigest())
            self.assertNotEqual(a["key"],b["key"])
            self.assertFalse(a["provisioned"])
            with self.assertRaises(FileExistsError): create(d,"bedroom-side-blinds")
            self.assertEqual(a,json.loads((d/"bedroom-side-blinds.json").read_text()))
    def test_reject_paths_and_invalid_names(self):
        with tempfile.TemporaryDirectory() as folder:
            for name in ["", "../secret", "Bedroom", "bad_thing", "-bad", "bad-", "a"*64]:
                with self.assertRaises(ValueError): create(Path(folder),name)
        with self.assertRaises(ValueError): create(Path(__file__).parent/"secrets","device")
if __name__=="__main__": unittest.main()
