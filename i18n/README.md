# Plasmid menu translations

## Contributing a language

A pull request adding a translated JSON file in this folder is welcome. Copy `en.json` to a file named after the language code, such as `it.json`, then translate the values while preserving the context names, source-string keys, and placeholders (`%1`, `%2`, `%n`). Include all messages and the plural forms your language needs. Mention the language and any regional scope in the PR.

Adding the JSON alone does not activate a language yet. The maintainer can handle catalog registration, plural-rule support, and tests during review; translation contributors do not need to edit C++. If you want to include that integration yourself, follow the notes below.

## Integration and maintenance

The executable embeds `en.json`, `pl.json`, `de.json`, `fr.json` and `es.json`. Qt's system UI-language preference list chooses the catalog at startup; English is the fallback. `src/translations.cpp` implements the public `QTranslator::translate()` interface, so QML `qsTr()` and C++ `tr()` keep their ordinary contexts. Catalogs are immutable after loading and can be read by metadata workers.

Each catalog maps a context (QML filename or C++ class name) to source strings and translations. Keep `%1`, `%2` and `%n` intact; parameters can be reordered. Do not translate application names, filenames, group IDs or user-created group names. The Pinned label is translated only for display.

Plural messages such as `%n items` use objects. English, German and Spanish use `one` for 1 and `other` otherwise. French uses `one` for 0/1 and `other` otherwise. Polish uses `one` for 1, `few` for numbers ending in 2–4 except 12–14, and `many` for the remaining integers. Qt substitutes `%n` after choosing the form. New languages with different rules need an appropriate selector in `Translations::translate()`.

For a new UI string, use `qsTr()`/`tr()` and add the source key to the matching context in all five catalogs. For plural strings, use `qsTr("%n items", "", count)` or the C++ numerus equivalent. Nonempty disambiguation comments are currently unsupported; use separate contexts for different meanings.

To add a language, register its catalog in `plasmid_add_translations()` in CMake and the supported-language list in `Translations`, then add locale/fallback tests. Builds need no `lupdate`/`lrelease` or extra packages. The translation tests check matching catalog keys, preserved placeholders, plural forms, system locale and ordered language fallback; UI tests render the added languages offscreen.

Run `cmake --build build -j 4` and `ctest --test-dir build --output-on-failure`. No live Plasmid instance is restarted by these tests.
