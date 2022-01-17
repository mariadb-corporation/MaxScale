/**
 * Tests database names with multi-byte unicode characters in them
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto other = test.maxscale->rwsplit();
    test.expect(other.connect(), "Failed to connect: %s", other.error());
    other.query("SET NAMES utf8mb4");

    auto test_cases = {
        // The original problem in MXS-3920
        "€uro",

        // The phrase "I can eat glass and it doesn't hurt me." in various languages
        // (from https://www.kermitproject.org/utf8.html)

        // Braille
        "⠊⠀⠉⠁⠝⠀⠑⠁⠞⠀⠛⠇⠁⠎⠎⠀⠁⠝⠙⠀⠊⠞⠀⠙⠕⠑⠎⠝⠞⠀⠓⠥⠗⠞⠀⠍⠑",
        // Japanese
        "私はガラスを食べられます。それは私を傷つけません。",
        // Inuktitut
        "ᐊᓕᒍᖅ ᓂᕆᔭᕌᖓᒃᑯ ᓱᕋᙱᑦᑐᓐᓇᖅᑐᖓ",
        // Korean
        "나는 유리를 먹을 수 있어요. 그래도 아프지 않아요",
        // Mongolian
        "Би шил идэй чадна, надад хортой биш",
        // Chinese
        "我能吞下玻璃而不伤身体。",
        // Tibetan
        "ཤེལ་སྒོ་ཟ་ནས་ང་ན་གི་མ་རེད།",
        // Yiddish
        "איך קען עסן גלאָז און עס טוט מיר נישט װײ",
        // Old Norse
        "ᛖᚴ ᚷᛖᛏ ᛖᛏᛁ ᚧ ᚷᛚᛖᚱ ᛘᚾ ᚦᛖᛋᛋ ᚨᚧ ᚡᛖ ᚱᚧᚨ ᛋᚨᚱ",
        // Old Irish
        "᚛᚛ᚉᚑᚅᚔᚉᚉᚔᚋ ᚔᚈᚔ ᚍᚂᚐᚅᚑ ᚅᚔᚋᚌᚓᚅᚐ᚜",
        // Burmese in Unicode 5.0 (only a part as it won't fit in the limit)
        "ကျွန်တော် ကျွန်မ မှန်စားနိုင်တယ်။",

        // TODO: Can only test utf8mb3 characters due to MDEV-27050, enable these once MDEV-27009 is fixed
        // Emoji
        // "🍣🍺"
        // Gothic
        // "𐌼𐌰𐌲 𐌲𐌻𐌴𐍃 𐌹̈𐍄𐌰𐌽, 𐌽𐌹 𐌼𐌹𐍃 𐍅𐌿 𐌽𐌳𐌰𐌽 𐌱𐍂𐌹𐌲𐌲𐌹𐌸",
    };

    // Create the databases in one go so that one user database update is enough
    for (std::string db : test_cases)
    {
        other.query("CREATE DATABASE `" + db + "`");
    }

    for (std::string db : test_cases)
    {
        auto c = test.maxscale->rwsplit();
        c.set_charset("utf8mb4");
        c.set_database(db);

        if (test.expect(c.connect(), "Failed to connect with database %s: %s", db.c_str(), c.error()))
        {
            test.expect(c.query("SELECT 1"), "Failed to query: %s", c.error());
        }
    }

    for (std::string db : test_cases)
    {
        other.query("DROP DATABASE `" + db + "`");
    }
}
