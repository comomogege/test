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
#include "lang.h"

#include "aboutbox.h"
#include "mainwidget.h"
#include "mainwindow.h"

#include "autoupdater.h"
#include "boxes/confirmbox.h"

#include "application.h"

AboutBox::AboutBox() : AbstractBox(st::aboutWidth)
, _version(this, lng_about_version(lt_version, QString::fromLatin1(AppVersionStr.c_str()) + (cAlphaVersion() ? " alpha" : "") + (cBetaVersion() ? qsl(" beta %1").arg(cBetaVersion()) : QString())), st::aboutVersionLink)
, _text1(this, lang(lng_about_text_1), FlatLabel::InitType::Rich, st::aboutLabel, st::aboutTextStyle)
, _text2(this, lang(lng_about_text_2), FlatLabel::InitType::Rich, st::aboutLabel, st::aboutTextStyle)
, _text3(this,st::aboutLabel, st::aboutTextStyle)
, _done(this, lang(lng_close), st::defaultBoxButton) {
	_text3.setRichText(lng_about_text_3(lt_faq_open, qsl("[a href=\"%1\"]").arg(telegramFaqLink()), lt_faq_close, qsl("[/a]")));

	setMaxHeight(st::boxTitleHeight + st::aboutTextTop + _text1.height() + st::aboutSkip + _text2.height() + st::aboutSkip + _text3.height() + st::boxButtonPadding.top() + _done.height() + st::boxButtonPadding.bottom());

	connect(&_version, SIGNAL(clicked()), this, SLOT(onVersion()));
	connect(&_done, SIGNAL(clicked()), this, SLOT(onClose()));

	prepare();

	setAcceptDrops(true);
}

void AboutBox::showAll() {
	_version.show();
	_text1.show();
	_text2.show();
	_text3.show();
	_done.show();
}

void AboutBox::resizeEvent(QResizeEvent *e) {
	_version.moveToLeft(st::boxPadding.left(), st::boxTitleHeight + st::aboutVersionTop);
	_text1.moveToLeft(st::boxPadding.left(), st::boxTitleHeight + st::aboutTextTop);
	_text2.moveToLeft(st::boxPadding.left(), _text1.y() + _text1.height() + st::aboutSkip);
	_text3.moveToLeft(st::boxPadding.left(), _text2.y() + _text2.height() + st::aboutSkip);
	_done.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _done.height());
	AbstractBox::resizeEvent(e);
}

void AboutBox::onVersion() {
	if (cRealBetaVersion()) {
		QString url = qsl("https://tdesktop.com/");
		switch (cPlatform()) {
		case dbipWindows: url += qsl("win/%1.zip"); break;
		case dbipMac: url += qsl("mac/%1.zip"); break;
		case dbipMacOld: url += qsl("mac32/%1.zip"); break;
		case dbipLinux32: url += qsl("linux32/%1.tar.xz"); break;
		case dbipLinux64: url += qsl("linux/%1.tar.xz"); break;
		}
		url = url.arg(qsl("tbeta%1_%2").arg(cRealBetaVersion()).arg(countBetaVersionSignature(cRealBetaVersion())));

		Application::clipboard()->setText(url);

		Ui::showLayer(new InformBox("The link to the current private beta version of Telegram Desktop was copied to the clipboard."));
	} else {
		QDesktopServices::openUrl(qsl("https://desktop.telegram.org/?_hash=changelog"));
	}
}

void AboutBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onClose();
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void AboutBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, qsl("Telegram Desktop"));
}

#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
QString _getCrashReportFile(const QMimeData *m) {
	if (!m || m->urls().size() != 1 || !m->urls().at(0).isLocalFile()) return QString();

	auto file = psConvertFileUrl(m->urls().at(0));

	return file.endsWith(qstr(".telegramcrash"), Qt::CaseInsensitive) ? file : QString();
}
#endif // !TDESKTOP_DISABLE_CRASH_REPORTS

void AboutBox::dragEnterEvent(QDragEnterEvent *e) {
#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
	if (!_getCrashReportFile(e->mimeData()).isEmpty()) {
		e->setDropAction(Qt::CopyAction);
		e->accept();
	}
#endif // !TDESKTOP_DISABLE_CRASH_REPORTS
}

void AboutBox::dropEvent(QDropEvent *e) {
#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
	if (!_getCrashReportFile(e->mimeData()).isEmpty()) {
		e->acceptProposedAction();
		showCrashReportWindow(_getCrashReportFile(e->mimeData()));
	}
#endif // !TDESKTOP_DISABLE_CRASH_REPORTS
}

QString telegramFaqLink() {
	QString result = qsl("https://telegram.org/faq");
	if (cLang() > languageDefault && cLang() < languageCount) {
		const char *code = LanguageCodes[cLang()].c_str();
		if (qstr("de") == code || qstr("es") == code || qstr("it") == code || qstr("ko") == code) {
			result += qsl("/") + code;
		} else if (qstr("pt_BR") == code) {
			result += qsl("/br");
		}
	}
	return result;
}
