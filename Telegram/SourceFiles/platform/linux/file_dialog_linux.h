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

#include "ui/filedialog.h"

extern "C" {
#undef signals
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#define signals public
} // extern "C"

namespace Platform {
namespace FileDialog {

bool Supported();

bool Get(QStringList &files, QByteArray &remoteContent, const QString &caption, const QString &filter, ::FileDialog::internal::Type type, QString startFile);

namespace internal {

// This is a patched copy of qgtk2 theme plugin.
// We need to use our own gtk file dialog instead of
// styling Qt file dialog, because Qt only works with gtk2.
// We need to be able to work with gtk2 and gtk3, because
// we use gtk3 to work with appindicator3.
class QGtkDialog : public QWindow {
    Q_OBJECT

public:
    QGtkDialog(GtkWidget *gtkWidget);
    ~QGtkDialog();

    GtkDialog *gtkDialog() const;

    void exec();
    void show(Qt::WindowFlags flags, Qt::WindowModality modality, QWindow *parent);
    void hide();

signals:
    void accept();
    void reject();

protected:
    static void onResponse(QGtkDialog *dialog, int response);

private slots:
    void onParentWindowDestroyed();

private:
    GtkWidget *gtkWidget;

};

class GtkFileDialog : public QDialog {
    Q_OBJECT

public:
    GtkFileDialog(QWidget *parent = Q_NULLPTR,
				  const QString &caption = QString(),
                  const QString &directory = QString(),
                  const QString &filter = QString());
    ~GtkFileDialog();

	void setVisible(bool visible) override;

	void setWindowTitle(const QString &windowTitle) {
		_windowTitle = windowTitle;
	}
	void setAcceptMode(QFileDialog::AcceptMode acceptMode) {
		_acceptMode = acceptMode;
	}
	void setFileMode(QFileDialog::FileMode fileMode) {
		_fileMode = fileMode;
	}
	void setOption(QFileDialog::Option option, bool on = true) {
		if (on) {
			_options |= option;
		} else {
			_options &= ~option;
		}
	}

    int exec() override;

    bool defaultNameFilterDisables() const;
    void setDirectory(const QString &directory);
    QDir directory() const;
    void selectFile(const QString &filename);
    QStringList selectedFiles() const;
    void setFilter();
    void selectNameFilter(const QString &filter);
    QString selectedNameFilter() const;

private slots:
    void onAccepted();
	void onRejected();

private:
    static void onSelectionChanged(GtkDialog *dialog, GtkFileDialog *helper);
    static void onCurrentFolderChanged(GtkFileDialog *helper);
    void applyOptions();
    void setNameFilters(const QStringList &filters);

	void showHelper(Qt::WindowFlags flags, Qt::WindowModality modality, QWindow *parent);
	void hideHelper();

	// Options
	QFileDialog::Options _options = { 0 };
	QString _windowTitle = "Choose file";
	QString _initialDirectory;
	QStringList _initialFiles;
	QStringList _nameFilters;
	QFileDialog::AcceptMode _acceptMode = QFileDialog::AcceptOpen;
	QFileDialog::FileMode _fileMode = QFileDialog::ExistingFile;

    QString _dir;
    QStringList _selection;
    QHash<QString, GtkFileFilter*> _filters;
    QHash<GtkFileFilter*, QString> _filterNames;
    QScopedPointer<QGtkDialog> d;
};

} // namespace internal
} // namespace FileDialog
} // namespace Platform
