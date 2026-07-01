#include "lottie-style-library.h"

#include <obs-frontend-api.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>
#include <QAbstractItemView>

static QStringList split_style_files(const QString &files)
{
	QStringList result = files.split(QRegularExpression("[\\r\\n;]+"), Qt::SkipEmptyParts);

	for (QString &path : result)
		path = path.trimmed();

	result.removeAll(QString());
	result.removeDuplicates();
	return result;
}

static QString join_style_files(const QListWidget *list)
{
	QStringList paths;
	for (int i = 0; i < list->count(); i++)
		paths << list->item(i)->text();

	paths.removeDuplicates();
	return paths.join('\n');
}

static void add_files_to_list(QListWidget *list, const QStringList &paths)
{
	QStringList existing;
	for (int i = 0; i < list->count(); i++)
		existing << list->item(i)->text();

	for (const QString &path : paths) {
		if (path.isEmpty() || existing.contains(path))
			continue;

		list->addItem(path);
		existing << path;
	}
}

static void add_valid_files_to_list(QWidget *parent, QListWidget *list, const QStringList &paths)
{
	QStringList valid_paths;
	int skipped = 0;

	for (const QString &path : paths) {
		if (lottie_style_library_is_valid_lottie_file(path.toUtf8().constData()))
			valid_paths << path;
		else
			skipped++;
	}

	add_files_to_list(list, valid_paths);

	if (skipped > 0) {
		QMessageBox::warning(
			parent, QStringLiteral("Invalid Lottie JSON"),
			QStringLiteral("%1 selected JSON file(s) were not valid Lottie animations and were ignored.")
				.arg(skipped));
	}
}

static void open_style_manager_dialog(void *)
{
	QWidget *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
	QDialog dialog(parent);
	dialog.setWindowTitle(QStringLiteral("Lottie Lower Third Styles"));
	dialog.resize(640, 420);

	auto *layout = new QVBoxLayout(&dialog);

	auto *dir_label = new QLabel(QStringLiteral("Style directory"), &dialog);
	auto *dir_row = new QHBoxLayout();
	auto *dir_edit = new QLineEdit(QString::fromUtf8(lottie_style_library_get_dir()), &dialog);
	auto *dir_browse = new QPushButton(QStringLiteral("Browse..."), &dialog);

	dir_row->addWidget(dir_edit, 1);
	dir_row->addWidget(dir_browse);

	auto *files_label = new QLabel(QStringLiteral("Individual style files"), &dialog);
	auto *files_list = new QListWidget(&dialog);
	files_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	add_files_to_list(files_list, split_style_files(QString::fromUtf8(lottie_style_library_get_files())));

	auto *file_buttons = new QHBoxLayout();
	auto *add_files = new QPushButton(QStringLiteral("Add JSON Files..."), &dialog);
	auto *remove_files = new QPushButton(QStringLiteral("Remove"), &dialog);
	file_buttons->addWidget(add_files);
	file_buttons->addWidget(remove_files);
	file_buttons->addStretch(1);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);

	layout->addWidget(dir_label);
	layout->addLayout(dir_row);
	layout->addSpacing(8);
	layout->addWidget(files_label);
	layout->addWidget(files_list, 1);
	layout->addLayout(file_buttons);
	layout->addWidget(buttons);

	QObject::connect(dir_browse, &QPushButton::clicked, [&dialog, dir_edit]() {
		QString dir = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("Select Style Directory"),
								dir_edit->text());
		if (!dir.isEmpty())
			dir_edit->setText(dir);
	});

	QObject::connect(add_files, &QPushButton::clicked, [&dialog, files_list]() {
		QStringList paths = QFileDialog::getOpenFileNames(&dialog, QStringLiteral("Add Lottie JSON Styles"),
								  QString(), QStringLiteral("Lottie files (*.json)"));
		add_valid_files_to_list(&dialog, files_list, paths);
	});

	QObject::connect(remove_files, &QPushButton::clicked,
			 [files_list]() { qDeleteAll(files_list->selectedItems()); });

	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	if (dialog.exec() == QDialog::Accepted) {
		lottie_style_library_set(join_style_files(files_list).toUtf8().constData(),
					 dir_edit->text().trimmed().toUtf8().constData());
	}
}

extern "C" void register_lottie_style_manager(void)
{
	obs_frontend_add_tools_menu_item("Lottie Lower Third Styles...", open_style_manager_dialog, nullptr);
}

extern "C" void unregister_lottie_style_manager(void) {}
