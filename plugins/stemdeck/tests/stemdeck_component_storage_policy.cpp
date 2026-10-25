#include "ComponentStoragePolicy.h"
#include <iostream>

int main (int argc, char** argv)
{
    // The supervising runner supplies a fresh, approved workspace directory.
    // Keep all fixture files, including failures, for independent review.
    if (argc != 2)
        return 2;
    const juce::File base (juce::String::fromUTF8 (argv[1]));
    if (! base.isDirectory())
        return 2;
    int failures = 0;
    auto check = [&] (bool value, const char* name)
    {
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
        failures += ! value;
    };
    const auto root = base.getChildFile ("policy-root");
    const auto sibling = base.getChildFile ("policy-root-sibling");
    const auto alias = base.getChildFile ("policy-alias");
    if (root.exists() || sibling.exists() || alias.exists() || alias.isSymbolicLink())
        return 2;
    if (root.createDirectory().failed() || sibling.createDirectory().failed())
        return 2;
    const auto policy = ComponentStoragePolicy::restrictedFixture (root);
    check (policy.isRestricted() && policy.isValid(), "explicit root admitted");
    check (! ComponentStoragePolicy::restrictedFixture ({}).isValid(), "empty root rejected");
    check (! ComponentStoragePolicy::restrictedFixture (root.getChildFile ("missing")).isValid(), "missing root rejected");
    check (policy.permitsPath (root.getChildFile ("new/sub/file")), "new descendant accepted");
    check (! policy.permitsPath (sibling.getChildFile ("file")), "sibling prefix rejected");
    check (! policy.permitsPath (root.getChildFile ("../escape")), "parent escape rejected");
    check (! policy.permitsPath ({}), "empty path rejected");
    const auto file = root.getChildFile ("regular-file");
    if (! file.replaceWithText ("fixture"))
        return 2;
    check (! ComponentStoragePolicy::restrictedFixture (file).isValid(), "file root rejected");
    if (! root.createSymbolicLink (alias, false))
        return 2;
    check (! ComponentStoragePolicy::restrictedFixture (alias).isValid(), "aliased root rejected");
    const auto escaped = root.getChildFile ("escape-link");
    if (! sibling.createSymbolicLink (escaped, false))
        return 2;
    check (! policy.permitsPath (escaped.getChildFile ("file")), "symlink descendant rejected");
    check (! ComponentStoragePolicy::production().isRestricted(), "production defaults retained");
    return failures == 0 ? 0 : 1;
}
