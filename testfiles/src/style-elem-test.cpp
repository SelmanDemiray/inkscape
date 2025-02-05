// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Test the API to the style element, access, read and write functions.
 *//*
 *
 * Authors:
 *   Martin Owens
 *
 * Copyright (C) 2018 Authors
 *
 * Released under GNU GPL version 2 or later, read the file 'COPYING' for more information
 */

#include <gtest/gtest.h>
#include <doc-per-case-test.h>

#include <src/style.h>
#include <src/object/sp-root.h>
#include <src/object/sp-style-elem.h>

using namespace Inkscape;
using namespace Inkscape::XML;
using namespace std::literals;

class ObjectTest: public DocPerCaseTest {
public:
    ObjectTest() {
        constexpr auto docString = R"A(
<svg xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink'>
<style id='style01'>
rect { fill: red; opacity:0.5; }
#id1, #id2 { fill: red; stroke: #c0c0c0; }
.cls1 { fill: red; opacity:1.0; }
</style>
<style id='style02'>
rect { fill: green; opacity:1.0; }
#id3, #id4 { fill: green; stroke: #606060; }
.cls2 { fill: green; opacity:0.5; }
</style>
</svg>)A"sv;
        doc = SPDocument::createNewDocFromMem(docString, false);
    }

    std::unique_ptr<SPDocument> doc;
};

/*
 * Test sp-style-element objects created in document.
 */
TEST_F(ObjectTest, StyleElems) {
    ASSERT_TRUE(doc);
    ASSERT_TRUE(doc->getRoot());

    SPRoot *root = doc->getRoot();
    ASSERT_TRUE(root->getRepr());

    auto one = cast<SPStyleElem>(doc->getObjectById("style01"));
    ASSERT_TRUE(one);

    for (auto &style : one->get_styles()) {
        EXPECT_EQ(style->fill.get_value(), Glib::ustring("#ff0000"));
    }

    auto two = cast<SPStyleElem>(doc->getObjectById("style02"));
    ASSERT_TRUE(one);

    for (auto &style : two->get_styles()) {
        EXPECT_EQ(style->fill.get_value(), Glib::ustring("#008000"));
    }
}
