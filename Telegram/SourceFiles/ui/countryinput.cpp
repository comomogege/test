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
#include "stdafx.h"
#include "ui/countryinput.h"

#include "lang.h"
#include "application.h"
#include "ui/scrollarea.h"
#include "ui/widgets/multi_select.h"
#include "boxes/contactsbox.h"
#include "countries.h"
#include "styles/style_boxes.h"

namespace {

	typedef QList<const CountryInfo *> CountriesFiltered;
	typedef QVector<int> CountriesIds;
	typedef QHash<QChar, CountriesIds> CountriesByLetter;
	typedef QVector<QString> CountryNames;
	typedef QVector<CountryNames> CountriesNames;

	CountriesByCode _countriesByCode;
	CountriesByISO2 _countriesByISO2;
	CountriesFiltered countriesFiltered, countriesAll, *countriesNow = &countriesAll;
	CountriesByLetter countriesByLetter;
	CountriesNames countriesNames;

	QString lastValidISO;
	int countriesCount = sizeof(countries) / sizeof(countries[0]);

	void initCountries() {
		if (!_countriesByCode.isEmpty()) return;

		_countriesByCode.reserve(countriesCount);
		_countriesByISO2.reserve(countriesCount);
		for (int i = 0; i < countriesCount; ++i) {
			const CountryInfo *info(countries + i);
			_countriesByCode.insert(info->code, info);
			CountriesByISO2::const_iterator already = _countriesByISO2.constFind(info->iso2);
			if (already != _countriesByISO2.cend()) {
				QString badISO = info->iso2;
				(void)badISO;
			}
			_countriesByISO2.insert(info->iso2, info);
		}
		countriesAll.reserve(countriesCount);
		countriesFiltered.reserve(countriesCount);
		countriesNames.resize(countriesCount);
	}

} // namespace

const CountriesByCode &countriesByCode() {
	initCountries();
	return _countriesByCode;
}

const CountriesByISO2 &countriesByISO2() {
	initCountries();
	return _countriesByISO2;
}

QString findValidCode(QString fullCode) {
	while (fullCode.length()) {
		CountriesByCode::const_iterator i = _countriesByCode.constFind(fullCode);
		if (i != _countriesByCode.cend()) {
			return (*i)->code;
		}
		fullCode = fullCode.mid(0, fullCode.length() - 1);
	}
	return "";
}

CountryInput::CountryInput(QWidget *parent, const style::countryInput &st) : QWidget(parent), _st(st), _active(false), _text(lang(lng_country_code)) {
	initCountries();

	resize(_st.width, _st.height + _st.ptrSize.height());
	QImage trImage(_st.ptrSize.width(), _st.ptrSize.height(), QImage::Format_ARGB32_Premultiplied);
	{
		static const QPoint trPoints[3] = {
			QPoint(0, 0),
			QPoint(_st.ptrSize.width(), 0),
			QPoint(qCeil(trImage.width() / 2.), trImage.height())
		};
		QPainter p(&trImage);
		p.setRenderHint(QPainter::Antialiasing);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(0, 0, trImage.width(), trImage.height(), st::transparent->b);

		p.setPen(Qt::NoPen);
		p.setBrush(_st.bgColor->b);
		p.drawPolygon(trPoints, 3);
	}
	_arrow = App::pixmapFromImageInPlace(std_::move(trImage));
	_inner = QRect(0, 0, _st.width, _st.height);
	_arrowRect = QRect((st::inpIntroCountryCode.width - _arrow.width() - 1) / 2, _st.height, _arrow.width(), _arrow.height());
}

void CountryInput::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	p.setRenderHint(QPainter::HighQualityAntialiasing);
	p.setBrush(_st.bgColor);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(_inner, st::buttonRadius, st::buttonRadius);
	p.setRenderHint(QPainter::HighQualityAntialiasing, false);

	p.drawPixmap(_arrowRect.x(), _arrowRect.top(), _arrow);

	p.setFont(_st.font);
	p.setPen(st::windowTextFg);

	p.drawText(rect().marginsRemoved(_st.textMrg), _text, QTextOption(_st.align));
}

void CountryInput::mouseMoveEvent(QMouseEvent *e) {
	bool newActive = _inner.contains(e->pos()) || _arrowRect.contains(e->pos());
	if (_active != newActive) {
		_active = newActive;
		setCursor(_active ? style::cur_pointer : style::cur_default);
	}
}

void CountryInput::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (_active) {
		CountrySelectBox *box = new CountrySelectBox();
		connect(box, SIGNAL(countryChosen(const QString&)), this, SLOT(onChooseCountry(const QString&)));
		Ui::showLayer(box);
	}
}

void CountryInput::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void CountryInput::leaveEvent(QEvent *e) {
	setMouseTracking(false);
	_active = false;
	setCursor(style::cur_default);
}

void CountryInput::onChooseCode(const QString &code) {
	Ui::hideLayer();
	if (code.length()) {
		CountriesByCode::const_iterator i = _countriesByCode.constFind(code);
		if (i != _countriesByCode.cend()) {
			const CountryInfo *info = *i;
			lastValidISO = info->iso2;
			setText(QString::fromUtf8(info->name));
		} else {
			setText(lang(lng_bad_country_code));
		}
	} else {
		setText(lang(lng_country_code));
	}
	update();
}

bool CountryInput::onChooseCountry(const QString &iso) {
	Ui::hideLayer();

	CountriesByISO2::const_iterator i = _countriesByISO2.constFind(iso);
	const CountryInfo *info = (i == _countriesByISO2.cend()) ? 0 : (*i);

	if (info) {
		lastValidISO = info->iso2;
		setText(QString::fromUtf8(info->name));
		emit codeChanged(info->code);
		update();
		return true;
	}
	return false;
}

void CountryInput::setText(const QString &newText) {
	_text = _st.font->elided(newText, width() - _st.textMrg.left() - _st.textMrg.right());
}

CountrySelectBox::CountrySelectBox() : ItemListBox(st::countriesScroll, st::boxWidth)
, _inner(this)
, _select(this, st::contactsMultiSelect, lang(lng_country_ph))
, _topShadow(this) {
	_select->resizeToWidth(st::boxWidth);

	ItemListBox::init(_inner, st::boxScrollSkip, st::boxTitleHeight + _select->height());

	_select->setQueryChangedCallback([this](const QString &query) { onFilterUpdate(query); });
	_select->setSubmittedCallback([this](bool) { onSubmit(); });
	connect(_inner, SIGNAL(mustScrollTo(int, int)), scrollArea(), SLOT(scrollToY(int, int)));
	connect(_inner, SIGNAL(countryChosen(const QString&)), this, SIGNAL(countryChosen(const QString&)));

	prepare();
}

void CountrySelectBox::onSubmit() {
	_inner->chooseCountry();
}

void CountrySelectBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Down) {
		_inner->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner->selectSkipPage(scrollArea()->height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner->selectSkipPage(scrollArea()->height(), -1);
	} else {
		ItemListBox::keyPressEvent(e);
	}
}

void CountrySelectBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_country_select));
}

void CountrySelectBox::resizeEvent(QResizeEvent *e) {
	ItemListBox::resizeEvent(e);

	_select->resizeToWidth(width());
	_select->moveToLeft(0, st::boxTitleHeight);

	_inner->resizeToWidth(width());
	_topShadow.setGeometry(0, st::boxTitleHeight + _select->height(), width(), st::lineWidth);
}

void CountrySelectBox::showAll() {
	_select->show();
	_topShadow.show();
	ItemListBox::showAll();
}

void CountrySelectBox::onFilterUpdate(const QString &query) {
	scrollArea()->scrollToY(0);
	_inner->updateFilter(query);
}

void CountrySelectBox::doSetInnerFocus() {
	_select->setInnerFocus();
}

CountrySelectBox::Inner::Inner(QWidget *parent) : ScrolledWidget(parent)
, _rowHeight(st::countryRowHeight)
, _sel(0)
, _mouseSel(false) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	CountriesByISO2::const_iterator l = _countriesByISO2.constFind(lastValidISO);
	bool seenLastValid = false;
	int already = countriesAll.size();

	countriesByLetter.clear();
	const CountryInfo *lastValid = (l == _countriesByISO2.cend()) ? 0 : (*l);
	for (int i = 0; i < countriesCount; ++i) {
		const CountryInfo *ins = lastValid ? (i ? (countries + i - (seenLastValid ? 0 : 1)) : lastValid) : (countries + i);
		if (lastValid && i && ins == lastValid) {
			seenLastValid = true;
			++ins;
		}
		if (already > i) {
			countriesAll[i] = ins;
		} else {
			countriesAll.push_back(ins);
		}

		QStringList namesList = QString::fromUtf8(ins->name).toLower().split(QRegularExpression("[\\s\\-]"), QString::SkipEmptyParts);
		CountryNames &names(countriesNames[i]);
		int l = namesList.size();
		names.resize(0);
		names.reserve(l);
		for (int j = 0, l = namesList.size(); j < l; ++j) {
			QString name = namesList[j].trimmed();
			if (!name.length()) continue;

			QChar ch = name[0];
			CountriesIds &v(countriesByLetter[ch]);
			if (v.isEmpty() || v.back() != i) {
				v.push_back(i);
			}

			names.push_back(name);
		}
	}

	_filter = qsl("a");
	updateFilter();
}

void CountrySelectBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r(e->rect());
	p.setClipRect(r);

	int l = countriesNow->size();
	if (l) {
		if (r.intersects(QRect(0, 0, width(), st::countriesSkip))) {
			p.fillRect(r.intersected(QRect(0, 0, width(), st::countriesSkip)), st::white->b);
		}
		int32 from = floorclamp(r.y() - st::countriesSkip, _rowHeight, 0, l);
		int32 to = ceilclamp(r.y() + r.height() - st::countriesSkip, _rowHeight, 0, l);
		for (int32 i = from; i < to; ++i) {
			bool sel = (i == _sel);
			int32 y = st::countriesSkip + i * _rowHeight;

			p.fillRect(0, y, width(), _rowHeight, (sel ? st::countryRowBgOver : st::white)->b);

			QString code = QString("+") + (*countriesNow)[i]->code;
			int32 codeWidth = st::countryRowCodeFont->width(code);

			QString name = QString::fromUtf8((*countriesNow)[i]->name);
			int32 nameWidth = st::countryRowNameFont->width(name);
			int32 availWidth = width() - st::countryRowPadding.left() - st::countryRowPadding.right() - codeWidth - st::contactsScroll.width;
			if (nameWidth > availWidth) {
				name = st::countryRowNameFont->elided(name, availWidth);
				nameWidth = st::countryRowNameFont->width(name);
			}

			p.setFont(st::countryRowNameFont);
			p.setPen(st::black);
			p.drawTextLeft(st::countryRowPadding.left(), y + st::countryRowPadding.top(), width(), name);
			p.setFont(st::countryRowCodeFont);
			p.setPen(sel ? st::countryRowCodeFgOver : st::countryRowCodeFg);
			p.drawTextLeft(st::countryRowPadding.left() + nameWidth + st::countryRowPadding.right(), y + st::countryRowPadding.top(), width(), code);
		}
	} else {
		p.fillRect(r, st::white->b);
		p.setFont(st::noContactsFont->f);
		p.setPen(st::noContactsColor->p);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_country_none), style::al_center);
	}
}

void CountrySelectBox::Inner::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void CountrySelectBox::Inner::leaveEvent(QEvent *e) {
	_mouseSel = false;
	setMouseTracking(false);
	if (_sel >= 0) {
		updateSelectedRow();
		_sel = -1;
	}
}

void CountrySelectBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
}

void CountrySelectBox::Inner::mousePressEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
	if (e->button() == Qt::LeftButton) {
		chooseCountry();
	}
}

void CountrySelectBox::Inner::updateFilter(QString filter) {
	filter = textSearchKey(filter);

	QStringList f;
	if (!filter.isEmpty()) {
		QStringList filterList = filter.split(cWordSplit(), QString::SkipEmptyParts);
		int l = filterList.size();

		f.reserve(l);
		for (int i = 0; i < l; ++i) {
			QString filterName = filterList[i].trimmed();
			if (filterName.isEmpty()) continue;
			f.push_back(filterName);
		}
		filter = f.join(' ');
	}
	if (_filter != filter) {
		_filter = filter;

		if (_filter.isEmpty()) {
			countriesNow = &countriesAll;
		} else {
			QChar first = _filter[0].toLower();
			CountriesIds &ids(countriesByLetter[first]);

			QStringList::const_iterator fb = f.cbegin(), fe = f.cend(), fi;

			countriesFiltered.clear();
			for (CountriesIds::const_iterator i = ids.cbegin(), e = ids.cend(); i != e; ++i) {
				int index = *i;
				CountryNames &names(countriesNames[index]);
				CountryNames::const_iterator nb = names.cbegin(), ne = names.cend(), ni;
				for (fi = fb; fi != fe; ++fi) {
					QString filterName(*fi);
					for (ni = nb; ni != ne; ++ni) {
						if (ni->startsWith(*fi)) {
							break;
						}
					}
					if (ni == ne) {
						break;
					}
				}
				if (fi == fe) {
					countriesFiltered.push_back(countriesAll[index]);
				}
			}
			countriesNow = &countriesFiltered;
		}
		refresh();
		_sel = countriesNow->isEmpty() ? -1 : 0;
		update();
	}
}

void CountrySelectBox::Inner::selectSkip(int32 dir) {
	_mouseSel = false;

	int cur = (_sel >= 0) ? _sel : -1;
	cur += dir;
	if (cur <= 0) {
		_sel = countriesNow->isEmpty() ? -1 : 0;
	} else if (cur >= countriesNow->size()) {
		_sel = -1;
	} else {
		_sel = cur;
	}
	if (_sel >= 0) {
		emit mustScrollTo(st::countriesSkip + _sel * _rowHeight, st::countriesSkip + (_sel + 1) * _rowHeight);
	}
	update();
}

void CountrySelectBox::Inner::selectSkipPage(int32 h, int32 dir) {
	int32 points = h / _rowHeight;
	if (!points) return;
	selectSkip(points * dir);
}

void CountrySelectBox::Inner::chooseCountry() {
	QString result;
	if (_filter.isEmpty()) {
		if (_sel >= 0 && _sel < countriesAll.size()) {
			result = countriesAll[_sel]->iso2;
		}
	} else {
		if (_sel >= 0 && _sel < countriesFiltered.size()) {
			result = countriesFiltered[_sel]->iso2;
		}
	}
	emit countryChosen(result);
}

void CountrySelectBox::Inner::refresh() {
	resize(width(), countriesNow->length() ? (countriesNow->length() * _rowHeight + st::countriesSkip) : st::noContactsHeight);
}

void CountrySelectBox::Inner::updateSel() {
	if (!_mouseSel) return;

	QPoint p(mapFromGlobal(_lastMousePos));
	bool in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(_lastMousePos));

	int32 newSel = (in && p.y() >= st::countriesSkip && p.y() < st::countriesSkip + countriesNow->size() * _rowHeight) ? ((p.y() - st::countriesSkip) / _rowHeight) : -1;
	if (newSel != _sel) {
		updateSelectedRow();
		_sel = newSel;
		updateSelectedRow();
	}
}

void CountrySelectBox::Inner::updateSelectedRow() {
	if (_sel >= 0) {
		update(0, st::countriesSkip + _sel * _rowHeight, width(), _rowHeight);
	}
}
