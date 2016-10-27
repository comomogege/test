/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include <memory>
#include <QtCore/QString>
#include <QtCore/QSet>
#include "codegen/common/cpp_file.h"
#include "codegen/style/structure_types.h"

namespace codegen {
namespace style {
namespace structure {
class Module;
} // namespace structure

class Generator {
public:
	Generator(const structure::Module &module, const QString &destBasePath, const common::ProjectInfo &project);
	Generator(const Generator &other) = delete;
	Generator &operator=(const Generator &other) = delete;

	bool writeHeader();
	bool writeSource();

private:
	QString typeToString(structure::Type type) const;
	QString typeToDefaultValue(structure::Type type) const;
	QString valueAssignmentCode(structure::Value value) const;

	bool writeHeaderStyleNamespace();
	bool writeStructsForwardDeclarations();
	bool writeStructsDefinitions();
	bool writeRefsDeclarations();

	bool writeIncludesInSource();
	bool writeVariableDefinitions();
	bool writeRefsDefinition();
	bool writeVariableInit();
	bool writePxValuesInit();
	bool writeFontFamiliesInit();
	bool writeIconValues();
	bool writeIconsInit();

	bool collectUniqueValues();

	const structure::Module &module_;
	QString basePath_, baseName_;
	const common::ProjectInfo &project_;
	std::unique_ptr<common::CppFile> source_, header_;

	QMap<int, bool> pxValues_;
	QMap<std::string, int> fontFamilies_;
	QMap<QString, int> iconMasks_; // icon file -> index

	std::vector<int> scales = { 4, 5, 6, 8 }; // scale / 4 gives our 1.00, 1.25, 1.50, 2.00
	std::vector<const char *>scaleNames = { "dbisOne", "dbisOneAndQuarter", "dbisOneAndHalf", "dbisTwo" };

};

} // namespace style
} // namespace codegen
