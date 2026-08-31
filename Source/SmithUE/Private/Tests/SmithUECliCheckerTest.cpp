// Copyright 2026, 123dx-svg. MIT License.
// SmithUE CLI Checker Automation Tests
//
// Tests the pure helper functions in SmithUECliInternal:
//   - ParseSemver   : various valid/invalid version strings
//   - CompareSemver : ordering including prerelease vs release
//   - ClassifyVersionProbe : rc + stdout -> trimmed version or empty
//   - ParseNpmLsCliVersion : npm ls --json parsing, rc ignored
//
// All helpers live in the SmithUECliInternal namespace and are exposed via
// the public header — no FRIEND_TEST needed.

#include "Tests/SmithUETestBase.h"
#include "Utils/SmithUECliChecker.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// ParseSemver
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_ParseSemver,
    "SmithUECliChecker.ParseSemver",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FTest_ParseSemver::RunTest(const FString& Parameters)
{
    using namespace SmithUECliInternal;

    // "1.2.3" -> {1,2,3,""}
    {
        auto R = ParseSemver(TEXT("1.2.3"));
        TestTrue(TEXT("1.2.3 parsed"), R.IsSet());
        if (R.IsSet())
        {
            TestEqual(TEXT("1.2.3 major"),      R->Major,      1);
            TestEqual(TEXT("1.2.3 minor"),      R->Minor,      2);
            TestEqual(TEXT("1.2.3 patch"),      R->Patch,      3);
            TestEqual(TEXT("1.2.3 prerelease"), R->Prerelease, FString(TEXT("")));
        }
    }

    // "0.13.0" -> {0,13,0,""}
    {
        auto R = ParseSemver(TEXT("0.13.0"));
        TestTrue(TEXT("0.13.0 parsed"), R.IsSet());
        if (R.IsSet())
        {
            TestEqual(TEXT("0.13.0 major"), R->Major, 0);
            TestEqual(TEXT("0.13.0 minor"), R->Minor, 13);
            TestEqual(TEXT("0.13.0 patch"), R->Patch, 0);
        }
    }

    // "0.13.0-rc.1" -> {0,13,0,"rc.1"}
    {
        auto R = ParseSemver(TEXT("0.13.0-rc.1"));
        TestTrue(TEXT("0.13.0-rc.1 parsed"), R.IsSet());
        if (R.IsSet())
        {
            TestEqual(TEXT("0.13.0-rc.1 major"),      R->Major,      0);
            TestEqual(TEXT("0.13.0-rc.1 minor"),      R->Minor,      13);
            TestEqual(TEXT("0.13.0-rc.1 patch"),      R->Patch,      0);
            TestEqual(TEXT("0.13.0-rc.1 prerelease"), R->Prerelease, FString(TEXT("rc.1")));
        }
    }

    // "garbage" -> empty
    {
        auto R = ParseSemver(TEXT("garbage"));
        TestFalse(TEXT("garbage -> empty"), R.IsSet());
    }

    // "" -> empty
    {
        auto R = ParseSemver(TEXT(""));
        TestFalse(TEXT("empty string -> empty"), R.IsSet());
    }

    // "v1.2.3" -> {1,2,3,""} (strip leading v)
    {
        auto R = ParseSemver(TEXT("v1.2.3"));
        TestTrue(TEXT("v1.2.3 parsed"), R.IsSet());
        if (R.IsSet())
        {
            TestEqual(TEXT("v1.2.3 major"),      R->Major,      1);
            TestEqual(TEXT("v1.2.3 minor"),      R->Minor,      2);
            TestEqual(TEXT("v1.2.3 patch"),      R->Patch,      3);
            TestEqual(TEXT("v1.2.3 prerelease"), R->Prerelease, FString(TEXT("")));
        }
    }

    // "1.2.3+build" -> {1,2,3,""} (ignore build metadata)
    {
        auto R = ParseSemver(TEXT("1.2.3+build.001"));
        TestTrue(TEXT("1.2.3+build parsed"), R.IsSet());
        if (R.IsSet())
        {
            TestEqual(TEXT("1.2.3+build major"),      R->Major,      1);
            TestEqual(TEXT("1.2.3+build minor"),      R->Minor,      2);
            TestEqual(TEXT("1.2.3+build patch"),      R->Patch,      3);
            TestEqual(TEXT("1.2.3+build prerelease"), R->Prerelease, FString(TEXT("")));
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// CompareSemver
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_CompareSemver,
    "SmithUECliChecker.CompareSemver",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FTest_CompareSemver::RunTest(const FString& Parameters)
{
    using namespace SmithUECliInternal;

    // {0,13,0,""} vs {0,13,0,""} -> 0  (equal)
    {
        FSmithUESemver A; A.Major=0; A.Minor=13; A.Patch=0; A.Prerelease=TEXT("");
        FSmithUESemver B; B.Major=0; B.Minor=13; B.Patch=0; B.Prerelease=TEXT("");
        TestEqual(TEXT("equal versions -> 0"), CompareSemver(A, B), (int32)0);
    }

    // {0,12,9,""} vs {0,13,0,""} -> -1  (minor less)
    {
        FSmithUESemver A; A.Major=0; A.Minor=12; A.Patch=9; A.Prerelease=TEXT("");
        FSmithUESemver B; B.Major=0; B.Minor=13; B.Patch=0; B.Prerelease=TEXT("");
        TestEqual(TEXT("0.12.9 < 0.13.0 -> -1"), CompareSemver(A, B), (int32)-1);
    }

    // {0,13,0,"rc.1"} vs {0,13,0,""} -> -1  (prerelease < release)
    {
        FSmithUESemver A; A.Major=0; A.Minor=13; A.Patch=0; A.Prerelease=TEXT("rc.1");
        FSmithUESemver B; B.Major=0; B.Minor=13; B.Patch=0; B.Prerelease=TEXT("");
        TestEqual(TEXT("0.13.0-rc.1 < 0.13.0 -> -1"), CompareSemver(A, B), (int32)-1);
    }

    // {0,13,1,""} vs {0,13,0,""} -> 1  (patch greater)
    {
        FSmithUESemver A; A.Major=0; A.Minor=13; A.Patch=1; A.Prerelease=TEXT("");
        FSmithUESemver B; B.Major=0; B.Minor=13; B.Patch=0; B.Prerelease=TEXT("");
        TestEqual(TEXT("0.13.1 > 0.13.0 -> 1"), CompareSemver(A, B), (int32)1);
    }

    return true;
}

// ---------------------------------------------------------------------------
// ClassifyVersionProbe
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_ClassifyVersionProbe,
    "SmithUECliChecker.ClassifyVersionProbe",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FTest_ClassifyVersionProbe::RunTest(const FString& Parameters)
{
    using namespace SmithUECliInternal;

    // (rc=0, "v10.8.1\n") -> "10.8.1"  (leading v stripped, newline trimmed)
    {
        auto R = ClassifyVersionProbe(0, TEXT("v10.8.1\n"));
        TestTrue(TEXT("v10.8.1 ok"), R.IsSet());
        if (R.IsSet())
        {
            TestEqual(TEXT("v10.8.1 stripped"), R.GetValue(), FString(TEXT("10.8.1")));
        }
    }

    // (rc=1, "") -> empty  (non-zero rc always fails)
    {
        auto R = ClassifyVersionProbe(1, TEXT(""));
        TestFalse(TEXT("rc=1 -> empty"), R.IsSet());
    }

    // (rc=0, "garbage") -> empty  (not a valid semver)
    {
        auto R = ClassifyVersionProbe(0, TEXT("garbage"));
        TestFalse(TEXT("garbage -> empty"), R.IsSet());
    }

    // (rc=0, " 10.8.1 ") -> "10.8.1"  (whitespace trimmed)
    {
        auto R = ClassifyVersionProbe(0, TEXT(" 10.8.1 "));
        TestTrue(TEXT("padded version ok"), R.IsSet());
        if (R.IsSet())
        {
            TestEqual(TEXT("padded trimmed"), R.GetValue(), FString(TEXT("10.8.1")));
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// ParseNpmLsCliVersion
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_ParseNpmLsCliVersion,
    "SmithUECliChecker.ParseNpmLsCliVersion",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FTest_ParseNpmLsCliVersion::RunTest(const FString& Parameters)
{
    using namespace SmithUECliInternal;

    // Valid JSON with smithue-cli present -> "0.13.0"
    {
        const FString Json = TEXT("{\"dependencies\":{\"smithue-cli\":{\"version\":\"0.13.0\"}}}");
        auto R = ParseNpmLsCliVersion(Json);
        TestTrue(TEXT("valid json: found"), R.IsSet());
        if (R.IsSet())
        {
            TestEqual(TEXT("valid json: version"), R.GetValue(), FString(TEXT("0.13.0")));
        }
    }

    // JSON without smithue-cli key -> empty
    {
        const FString Json = TEXT("{\"dependencies\":{\"some-other-pkg\":{\"version\":\"1.0.0\"}}}");
        auto R = ParseNpmLsCliVersion(Json);
        TestFalse(TEXT("missing smithue-cli key -> empty"), R.IsSet());
    }

    // rc!=0 scenario: function only receives the JSON string, not rc.
    // Passing valid JSON (as would appear even on rc=1 npm ls exit) must succeed.
    {
        // npm ls exits with rc=1 when a package is extraneous or has issues, but
        // still emits valid JSON. ParseNpmLsCliVersion ignores rc entirely.
        const FString Json = TEXT("{\"dependencies\":{\"smithue-cli\":{\"version\":\"0.13.0\"}}}");
        auto R = ParseNpmLsCliVersion(Json);
        TestTrue(TEXT("rc-ignored: json found regardless of caller rc"), R.IsSet());
        if (R.IsSet())
        {
            TestEqual(TEXT("rc-ignored: correct version"), R.GetValue(), FString(TEXT("0.13.0")));
        }
    }

    // Empty string -> empty
    {
        auto R = ParseNpmLsCliVersion(TEXT(""));
        TestFalse(TEXT("empty string -> empty"), R.IsSet());
    }

    // Malformed JSON -> empty
    {
        auto R = ParseNpmLsCliVersion(TEXT("{not: valid: json}"));
        TestFalse(TEXT("malformed json -> empty"), R.IsSet());
    }

    return true;
}

// ---------------------------------------------------------------------------
// ResolveTargetCliVersion
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_ResolveTargetCliVersion,
    "SmithUECliChecker.ResolveTargetCliVersion",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FTest_ResolveTargetCliVersion::RunTest(const FString& Parameters)
{
    using namespace SmithUECliInternal;

    // The whole point: a registry release newer than the hardcoded floor wins,
    // so shipping a CLI no longer requires bumping the plugin.
    TestEqual(TEXT("latest newer than floor -> latest"),
        ResolveTargetCliVersion(TEXT("0.15.0"), TEXT("0.16.2")), FString(TEXT("0.16.2")));

    // A floor above the registry (e.g. the plugin needs an unreleased CLI) holds.
    TestEqual(TEXT("floor newer than latest -> floor"),
        ResolveTargetCliVersion(TEXT("0.17.0"), TEXT("0.16.2")), FString(TEXT("0.17.0")));

    TestEqual(TEXT("equal -> floor"),
        ResolveTargetCliVersion(TEXT("0.15.0"), TEXT("0.15.0")), FString(TEXT("0.15.0")));

    // Offline / npm missing: the registry answer is empty, fall back to the floor.
    TestEqual(TEXT("no latest -> floor"),
        ResolveTargetCliVersion(TEXT("0.15.0"), TEXT("")), FString(TEXT("0.15.0")));

    // Garbage from npm must not become the target.
    TestEqual(TEXT("garbage latest -> floor"),
        ResolveTargetCliVersion(TEXT("0.15.0"), TEXT("ERR! network")), FString(TEXT("0.15.0")));

    TestEqual(TEXT("no floor -> latest"),
        ResolveTargetCliVersion(TEXT(""), TEXT("0.16.2")), FString(TEXT("0.16.2")));

    TestEqual(TEXT("neither -> empty"),
        ResolveTargetCliVersion(TEXT(""), TEXT("")), FString());

    // A prerelease on the registry must not out-rank the released floor.
    TestEqual(TEXT("prerelease latest vs release floor -> floor"),
        ResolveTargetCliVersion(TEXT("0.16.0"), TEXT("0.16.0-rc.1")), FString(TEXT("0.16.0")));

    return true;
}

// ---------------------------------------------------------------------------
// IsCliOutdated
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_IsCliOutdated,
    "SmithUECliChecker.IsCliOutdated",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FTest_IsCliOutdated::RunTest(const FString& Parameters)
{
    using namespace SmithUECliInternal;

    TestTrue (TEXT("below target -> outdated"),      IsCliOutdated(TEXT("0.15.0"), TEXT("0.16.2")));
    TestFalse(TEXT("at target -> not outdated"),     IsCliOutdated(TEXT("0.16.2"), TEXT("0.16.2")));
    TestFalse(TEXT("above target -> not outdated"),  IsCliOutdated(TEXT("0.17.0"), TEXT("0.16.2")));

    // No credible target (offline AND no floor): never claim outdated, otherwise a
    // disconnected machine would auto-reinstall on every startup.
    TestFalse(TEXT("empty target -> not outdated"),    IsCliOutdated(TEXT("0.15.0"), TEXT("")));
    TestFalse(TEXT("garbage target -> not outdated"),  IsCliOutdated(TEXT("0.15.0"), TEXT("nope")));

    // Unreadable installed version with a valid target: reinstall to repair.
    TestTrue(TEXT("garbage installed + valid target -> outdated"),
        IsCliOutdated(TEXT("???"), TEXT("0.16.2")));
    TestTrue(TEXT("empty installed + valid target -> outdated"),
        IsCliOutdated(TEXT(""), TEXT("0.16.2")));

    TestTrue(TEXT("prerelease below release target -> outdated"),
        IsCliOutdated(TEXT("0.16.0-rc.1"), TEXT("0.16.0")));

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
