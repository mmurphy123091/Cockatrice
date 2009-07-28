#ifndef DLG_SETTINGS_H
#define DLG_SETTINGS_H

#include <QDialog>

class CardDatabase;
class QTranslator;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QLineEdit;
class QPushButton;
class QComboBox;
class QGroupBox;
class QLabel;

class AbstractSettingsPage : public QWidget {
public:
	virtual void retranslateUi() = 0;
};

class GeneralSettingsPage : public AbstractSettingsPage {
	Q_OBJECT
public:
	GeneralSettingsPage();
	void retranslateUi();
private slots:
	void deckPathButtonClicked();
	void picsPathButtonClicked();
	void cardDatabasePathButtonClicked();
	void languageBoxChanged(int index);
signals:
	void picsPathChanged(const QString &path);
	void cardDatabasePathChanged(const QString &path);
	void changeLanguage(const QString &qmFile);
private:
	QStringList findQmFiles();
	QString languageName(const QString &qmFile);
	QLineEdit *deckPathEdit, *picsPathEdit, *cardDatabasePathEdit;
	QGroupBox *personalGroupBox, *pathsGroupBox;
	QComboBox *languageBox;
	QLabel *languageLabel, *deckPathLabel, *picsPathLabel, *cardDatabasePathLabel;
};

class AppearanceSettingsPage : public AbstractSettingsPage {
	Q_OBJECT
public:
	AppearanceSettingsPage();
	void retranslateUi();
};

class MessagesSettingsPage : public AbstractSettingsPage {
	Q_OBJECT
public:
	MessagesSettingsPage();
	void retranslateUi();
private slots:
	void actAdd();
	void actRemove();
private:
	QListWidget *messageList;
	QAction *aAdd, *aRemove;
	
	void storeSettings();
};

class DlgSettings : public QDialog {
	Q_OBJECT
public:
	DlgSettings(CardDatabase *_db, QTranslator *_translator, QWidget *parent = 0);
private slots:
	void changePage(QListWidgetItem *current, QListWidgetItem *previous);
	void changeLanguage(const QString &qmFile);
private:
	CardDatabase *db;
	QTranslator *translator;
	QListWidget *contentsWidget;
	QStackedWidget *pagesWidget;
	QListWidgetItem *generalButton, *appearanceButton, *messagesButton;
	QPushButton *closeButton;
	void createIcons();
	void retranslateUi();
protected:
	void changeEvent(QEvent *event);
};

#endif 
