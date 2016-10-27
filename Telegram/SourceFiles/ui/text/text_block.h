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

#include "private/qfontengine_p.h"

enum TextBlockType {
	TextBlockTNewline = 0x01,
	TextBlockTText = 0x02,
	TextBlockTEmoji = 0x03,
	TextBlockTSkip = 0x04,
};

enum TextBlockFlags {
	TextBlockFBold = 0x01,
	TextBlockFItalic = 0x02,
	TextBlockFUnderline = 0x04,
	TextBlockFTilde = 0x08, // tilde fix in OpenSans
	TextBlockFSemibold = 0x10,
	TextBlockFCode = 0x20,
	TextBlockFPre = 0x40,
};

class ITextBlock {
public:

	ITextBlock(const style::font &font, const QString &str, uint16 from, uint16 length, uchar flags, const style::color &color, uint16 lnkIndex) : _from(from), _flags((flags & 0xFF) | ((lnkIndex & 0xFFFF) << 12))/*, _color(color)*/, _lpadding(0) {
		if (length) {
			if (str.at(_from + length - 1).unicode() == QChar::Space) {
				_rpadding = font->spacew;
			}
			if (length > 1 && str.at(0).unicode() == QChar::Space) {
				_lpadding = font->spacew;
			}
		}
	}

	uint16 from() const {
		return _from;
	}
	int32 width() const {
		return _width.toInt();
	}
	int32 lpadding() const {
		return _lpadding.toInt();
	}
	int32 rpadding() const {
		return _rpadding.toInt();
	}
	QFixed f_width() const {
		return _width;
	}
	QFixed f_lpadding() const {
		return _lpadding;
	}
	QFixed f_rpadding() const {
		return _rpadding;
	}

	// Should be virtual, but optimized throught type() call.
	QFixed f_rbearing() const;

	uint16 lnkIndex() const {
		return (_flags >> 12) & 0xFFFF;
	}
	void setLnkIndex(uint16 lnkIndex) {
		_flags = (_flags & ~(0xFFFF << 12)) | (lnkIndex << 12);
	}

	TextBlockType type() const {
		return TextBlockType((_flags >> 8) & 0x0F);
	}
	int32 flags() const {
		return (_flags & 0xFF);
	}
	const style::color &color() const {
		static style::color tmp;
		return tmp;//_color;
	}

	virtual ITextBlock *clone() const = 0;
	virtual ~ITextBlock() {
	}

protected:

	uint16 _from;

	uint32 _flags; // 4 bits empty, 16 bits lnkIndex, 4 bits type, 8 bits flags

	QFixed _width, _lpadding, _rpadding;

};

class NewlineBlock : public ITextBlock {
public:

	Qt::LayoutDirection nextDirection() const {
		return _nextDir;
	}

	ITextBlock *clone() const {
		return new NewlineBlock(*this);
	}

private:

	NewlineBlock(const style::font &font, const QString &str, uint16 from, uint16 length) : ITextBlock(font, str, from, length, 0, st::transparent, 0), _nextDir(Qt::LayoutDirectionAuto) {
		_flags |= ((TextBlockTNewline & 0x0F) << 8);
	}

	Qt::LayoutDirection _nextDir;

	friend class Text;
	friend class TextParser;

	friend class TextPainter;
};

class TextWord {
public:
	TextWord() = default;
	TextWord(uint16 from, QFixed width, QFixed rbearing, QFixed rpadding = 0)
		: _from(from)
		, _width(width)
		, _rpadding(rpadding)
		, _rbearing(rbearing.value() > 0x7FFF ? 0x7FFF : (rbearing.value() < -0x7FFF ? -0x7FFF : rbearing.value())) {
	}
	uint16 from() const {
		return _from;
	}
	QFixed f_rbearing() const {
		return QFixed::fromFixed(_rbearing);
	}
	QFixed f_width() const {
		return _width;
	}
	QFixed f_rpadding() const {
		return _rpadding;
	}
	void add_rpadding(QFixed padding) {
		_rpadding += padding;
	}

private:
	uint16 _from = 0;
	QFixed _width, _rpadding;
	int16 _rbearing = 0;

};

class TextBlock : public ITextBlock {
public:

	ITextBlock *clone() const {
		return new TextBlock(*this);
	}

private:

	TextBlock(const style::font &font, const QString &str, QFixed minResizeWidth, uint16 from, uint16 length, uchar flags, const style::color &color, uint16 lnkIndex);

	friend class ITextBlock;
	QFixed real_f_rbearing() const {
		return _words.isEmpty() ? 0 : _words.back().f_rbearing();
	}

	typedef QVector<TextWord> TextWords;
	TextWords _words;

	friend class Text;
	friend class TextParser;

	friend class BlockParser;
	friend class TextPainter;
};

class EmojiBlock : public ITextBlock {
public:

	ITextBlock *clone() const {
		return new EmojiBlock(*this);
	}

private:

	EmojiBlock(const style::font &font, const QString &str, uint16 from, uint16 length, uchar flags, const style::color &color, uint16 lnkIndex, const EmojiData *emoji);

	const EmojiData *emoji;

	friend class Text;
	friend class TextParser;

	friend class TextPainter;
};

class SkipBlock : public ITextBlock {
public:

	int32 height() const {
		return _height;
	}

	ITextBlock *clone() const {
		return new SkipBlock(*this);
	}

private:

	SkipBlock(const style::font &font, const QString &str, uint16 from, int32 w, int32 h, uint16 lnkIndex);

	int32 _height;

	friend class Text;
	friend class TextParser;

	friend class TextPainter;
};
