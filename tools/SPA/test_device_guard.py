import unittest

from spa_device_guard import authorize_client


class DeviceGuardTests(unittest.TestCase):
    def state(self, package):
        return ([f"topResumedActivity=ActivityRecord{{ u0 {package}/com.u8.sdk.U8UnityContext }}",
                 "topResumedActivity=ActivityRecord{ u0 app.lawnchair/.Launcher }"],
                [f"mCurrentFocus=Window{{ u0 {package}/com.u8.sdk.U8UnityContext }}"])

    def test_official_requires_explicit_matching_authorization(self):
        resumed, focused = self.state("com.hypergryph.arknights")
        self.assertEqual(authorize_client("Official", resumed, focused), "com.hypergryph.arknights")
        with self.assertRaises(ValueError):
            authorize_client("Bilibili", resumed, focused)

    def test_package_prefix_does_not_authorize_other_channel(self):
        resumed, focused = self.state("com.hypergryph.arknights.bilibili")
        self.assertEqual(authorize_client("Bilibili", resumed, focused), "com.hypergryph.arknights.bilibili")
        with self.assertRaises(ValueError):
            authorize_client("Official", resumed, focused)

    def test_focus_and_resumed_activity_must_both_match(self):
        official, official_focus = self.state("com.hypergryph.arknights")
        bili, bili_focus = self.state("com.hypergryph.arknights.bilibili")
        for resumed, focused in [(official, bili_focus), (bili, official_focus),
                                 (official, []), (official, official_focus * 2)]:
            with self.subTest(resumed=resumed, focused=focused), self.assertRaises(ValueError):
                authorize_client("Official", resumed, focused)

    def test_policy_cannot_override_authorized_client(self):
        resumed, focused = self.state("com.hypergryph.arknights")
        with self.assertRaises(ValueError):
            authorize_client("Official", resumed, focused, {"client_type": "Bilibili"})
        self.assertEqual(authorize_client("Official", resumed, focused, {}), "com.hypergryph.arknights")


if __name__ == "__main__":
    unittest.main()
