//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include "Opts.h"

#include <format>
#include <numeric>

#include <QLocale>

#include <magic_enum.hpp>

#include <Config.h>
#include "Error.h"

namespace Opts {

using namespace cxxopts;
using namespace magic_enum;

const LaunchOpts &LaunchOptsManager::Parse(int argc, char *argv[])
{
    try {
        Options parser{Config::ProgramName, Config::Description};

        parser.add_options()          //
            ("help", "Print options") //
            ("trace", "Enable trace level logging.", value<bool>()->default_value("false"));

        auto names = enum_names<PrintAllLocales>();
        auto namesStr = std::accumulate(
            names.begin(), names.end(), std::string{},
            [](const std::string_view &lhs, const std::string_view &rhs) -> std::string {
                return std::string{lhs} + (lhs.size() > 0 ? ", " : "") + std::string{rhs};
            });

        parser.add_options()(
            "print-all-locales", std::format("Print all locales for translators. [{}]", namesStr),
            value<std::string>()
                ->default_value(std::string{enum_name(PrintAllLocales::None)})
                ->implicit_value(std::string{enum_name(PrintAllLocales::LangOnly)}));

        //////////////////////////////////////////////////

        auto args = parser.parse(argc, argv);

        if (args.count("help")) {
            std::cout << parser.help() << std::endl;
            std::exit(0);
        }

        _opts.enableTrace = args["trace"].as<bool>();

        auto printAllLocales =
            enum_cast<PrintAllLocales>(args["print-all-locales"].as<std::string>());
        if (!printAllLocales.has_value()) {
            std::cerr << "Invalid argument for `print-all-locales`, expected one of [" << namesStr
                      << "]." << std::endl;
            std::exit(1);
        }
        HandlePrintAllLocales(printAllLocales.value());

        return _opts;
    }
    catch (cxxopts::OptionException &exception) {
        FatalError(std::format("Parse options failed.\n\n{}", exception.what()), false);
        std::exit(1);
    }
}

void LaunchOptsManager::HandlePrintAllLocales(PrintAllLocales action)
{
    if (action == PrintAllLocales::None) {
        return;
    }

    QList<QLocale> allLocales =
        QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyCountry);

    QList<QString> names;

    for (const QLocale &locale : allLocales) {
        QString name = locale.name();
        if (action == PrintAllLocales::LangOnly) {
            name = name.split('_').first();
        }
        if (!names.contains(name)) {
            names.push_back(name);
        }
    }

    std::cout << "allLocales (" << enum_name(action) << "):\n[";
    for (const auto &name : names) {
        std::cout << "\"" << name.toStdString() << "\", ";
    }
    std::cout << "]" << std::endl;

    std::exit(0);
}

} // namespace Opts
