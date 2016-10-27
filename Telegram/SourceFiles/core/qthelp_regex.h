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

namespace qthelp {

class RegularExpressionMatch {
public:
	RegularExpressionMatch(QRegularExpressionMatch &&match) : data_(std_::move(match)) {
	}
	RegularExpressionMatch(RegularExpressionMatch &&other) : data_(std_::move(other.data_)) {
	}
	QRegularExpressionMatch *operator->() {
		return &data_;
	}
	const QRegularExpressionMatch *operator->() const {
		return &data_;
	}
	explicit operator bool() const {
		return data_.hasMatch();
	}

private:
	QRegularExpressionMatch data_;

};

enum class RegExOption {
	None = QRegularExpression::NoPatternOption,
	CaseInsensitive = QRegularExpression::CaseInsensitiveOption,
	DotMatchesEverything = QRegularExpression::DotMatchesEverythingOption,
	Multiline = QRegularExpression::MultilineOption,
	ExtendedSyntax = QRegularExpression::ExtendedPatternSyntaxOption,
	InvertedGreediness = QRegularExpression::InvertedGreedinessOption,
	DontCapture = QRegularExpression::DontCaptureOption,
	UseUnicodeProperties = QRegularExpression::UseUnicodePropertiesOption,
#ifndef OS_MAC_OLD
	OptimizeOnFirstUsage = QRegularExpression::OptimizeOnFirstUsageOption,
	DontAutomaticallyOptimize = QRegularExpression::DontAutomaticallyOptimizeOption,
#endif // OS_MAC_OLD
};
Q_DECLARE_FLAGS(RegExOptions, RegExOption);
Q_DECLARE_OPERATORS_FOR_FLAGS(RegExOptions);

inline RegularExpressionMatch regex_match(const QString &string, const QString &subject, RegExOptions options = 0) {
	auto qtOptions = QRegularExpression::PatternOptions(static_cast<int>(options));
	return RegularExpressionMatch(QRegularExpression(string, qtOptions).match(subject));
}

inline RegularExpressionMatch regex_match(const QString &string, const QStringRef &subjectRef, RegExOptions options = 0) {
	auto qtOptions = QRegularExpression::PatternOptions(static_cast<int>(options));
#ifndef OS_MAC_OLD
	return RegularExpressionMatch(QRegularExpression(string, qtOptions).match(subjectRef));
#else // OS_MAC_OLD
	return RegularExpressionMatch(QRegularExpression(string, qtOptions).match(subjectRef.toString()));
#endif // OS_MAC_OLD
}

} // namespace qthelp
