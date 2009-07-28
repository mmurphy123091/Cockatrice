#include "carddatabase.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QSettings>
#include <QSvgRenderer>
#include <QPainter>

CardSet::CardSet(const QString &_shortName, const QString &_longName)
	: shortName(_shortName), longName(_longName)
{
	updateSortKey();
}

QXmlStreamWriter &operator<<(QXmlStreamWriter &xml, const CardSet *set)
{
	xml.writeStartElement("set");
	xml.writeTextElement("name", set->getShortName());
	xml.writeTextElement("longname", set->getLongName());
	xml.writeEndElement();

	return xml;
}

void CardSet::setSortKey(unsigned int _sortKey)
{
	sortKey = _sortKey;

	QSettings settings;
	settings.beginGroup("sets");
	settings.beginGroup(shortName);
	settings.setValue("sortkey", sortKey);
}

void CardSet::updateSortKey()
{
	QSettings settings;
	settings.beginGroup("sets");
	settings.beginGroup(shortName);
	sortKey = settings.value("sortkey", 0).toInt();
}

class SetList::CompareFunctor {
public:
	inline bool operator()(CardSet *a, CardSet *b) const
	{
		return a->getSortKey() < b->getSortKey();
	}
};

void SetList::sortByKey()
{
	qSort(begin(), end(), CompareFunctor());
}

CardInfo::CardInfo(CardDatabase *_db, const QString &_name, const QString &_manacost, const QString &_cardtype, const QString &_powtough, const QString &_text, int _tableRow, const SetList &_sets)
	: db(_db), name(_name), sets(_sets), manacost(_manacost), cardtype(_cardtype), powtough(_powtough), text(_text), tableRow(_tableRow), pixmap(NULL)
{
	for (int i = 0; i < sets.size(); i++)
		sets[i]->append(this);
}

CardInfo::~CardInfo()
{
	clearPixmapCache();
}

QString CardInfo::getMainCardType() const
{
	QString result = getCardType();
	/*
	Legendary Artifact Creature - Golem
	Instant // Instant
	*/

	int pos;
	if ((pos = result.indexOf('-')) != -1)
		result.remove(pos, result.length());
	if ((pos = result.indexOf("//")) != -1)
		result.remove(pos, result.length());
	result = result.simplified();
	/*
	Legendary Artifact Creature
	Instant
	*/

	if ((pos = result.lastIndexOf(' ')) != -1)
		result = result.mid(pos + 1);
	/*
	Creature
	Instant
	*/

	return result;
}

void CardInfo::addToSet(CardSet *set)
{
	set->append(this);
	sets << set;
}

QPixmap *CardInfo::loadPixmap()
{
	if (pixmap)
		return pixmap;
	QString picsPath = db->getPicsPath();
	pixmap = new QPixmap();
	if (getName().isEmpty()) {
		pixmap->load(QString("%1/back.jpg").arg(picsPath));
		return pixmap;
	}
	sets.sortByKey();

	QString debugOutput = QString("CardDatabase: loading pixmap for '%1' from ").arg(getName());
	for (int i = 0; i < sets.size(); i++)
		debugOutput.append(QString("%1, ").arg(sets[i]->getShortName()));
	qDebug(debugOutput.toLatin1());

	for (int i = 0; i < sets.size(); i++) {
		// Fire // Ice, Circle of Protection: Red
		QString correctedName = getName().remove(" // ").remove(":");
		if (pixmap->load(QString("%1/%2/%3.full.jpg").arg(picsPath).arg(sets[i]->getShortName()).arg(correctedName)))
			return pixmap;
		if (pixmap->load(QString("%1/%2/%3%4.full.jpg").arg(picsPath).arg(sets[i]->getShortName()).arg(correctedName).arg(1)))
			return pixmap;
	}
	return pixmap;
}

QPixmap *CardInfo::getPixmap(QSize size)
{
	qDebug(QString("CardInfo::getPixmap(%1, %2) for %3").arg(size.width()).arg(size.height()).arg(getName()).toLatin1());
	QPixmap *cachedPixmap = scaledPixmapCache.value(size.width());
	if (cachedPixmap)
		return cachedPixmap;
	QPixmap *bigPixmap = loadPixmap();
	QPixmap *result;
	if (bigPixmap->isNull()) {
		if (!getName().isEmpty())
			return 0;
		else {
			result = new QPixmap(size);
			result->fill(Qt::transparent);
			QSvgRenderer svg(QString(":/back.svg"));
			QPainter painter(result);
			svg.render(&painter, QRectF(0, 0, size.width(), size.height()));
		}
	} else
		result = new QPixmap(bigPixmap->scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
	scaledPixmapCache.insert(size.width(), result);
	return result;
}

void CardInfo::clearPixmapCache()
{
	if (pixmap) {
		qDebug(QString("Deleting pixmap for %1").arg(name).toLatin1());
		delete pixmap;
		pixmap = 0;
		QMapIterator<int, QPixmap *> i(scaledPixmapCache);
		while (i.hasNext()) {
			i.next();
			qDebug(QString("  Deleting cached pixmap for width %1").arg(i.key()).toLatin1());
			delete i.value();
		}
		scaledPixmapCache.clear();
	}
}

QXmlStreamWriter &operator<<(QXmlStreamWriter &xml, const CardInfo *info)
{
	xml.writeStartElement("card");
	xml.writeTextElement("name", info->getName());
	
	SetList sets = info->getSets();
	for (int i = 0; i < sets.size(); i++)
		xml.writeTextElement("set", sets[i]->getShortName());
	
	xml.writeTextElement("manacost", info->getManaCost());
	xml.writeTextElement("type", info->getCardType());
	if (!info->getPowTough().isEmpty())
		xml.writeTextElement("pt", info->getPowTough());
	xml.writeTextElement("tablerow", QString::number(info->getTableRow()));
	xml.writeTextElement("text", info->getText());
	xml.writeEndElement(); // card
	
	return xml;
}

CardDatabase::CardDatabase(QObject *parent)
	: QObject(parent), noCard(0)
{
	updateDatabasePath();
	updatePicsPath();
	
	noCard = new CardInfo(this);
	noCard->loadPixmap(); // cache pixmap for card back
}

CardDatabase::~CardDatabase()
{
	clear();
}

void CardDatabase::clear()
{
	QHashIterator<QString, CardSet *> setIt(setHash);
	while (setIt.hasNext()) {
		setIt.next();
		delete setIt.value();
	}
	setHash.clear();

	QHashIterator<QString, CardInfo *> i(cardHash);
	while (i.hasNext()) {
		i.next();
		delete i.value();
	}
	cardHash.clear();
}

CardInfo *CardDatabase::getCard(const QString &cardName)
{
	if (cardName.isEmpty())
		return noCard;
	else if (cardHash.contains(cardName))
		return cardHash.value(cardName);
	else {
		qDebug(QString("CardDatabase: card not found: %1").arg(cardName).toLatin1());
		CardInfo *newCard = new CardInfo(this, cardName);
		newCard->addToSet(getSet("TK"));
		cardHash.insert(cardName, newCard);
		return newCard;
	}
}

CardSet *CardDatabase::getSet(const QString &setName)
{
	if (setHash.contains(setName))
		return setHash.value(setName);
	else {
		qDebug(QString("CardDatabase: set not found: %1").arg(setName).toLatin1());
		CardSet *newSet = new CardSet(setName);
		setHash.insert(setName, newSet);
		return newSet;
	}
}

SetList CardDatabase::getSetList() const
{
	SetList result;
	QHashIterator<QString, CardSet *> i(setHash);
	while (i.hasNext()) {
		i.next();
		result << i.value();
	}
	return result;
}

void CardDatabase::clearPixmapCache()
{
	QHashIterator<QString, CardInfo *> i(cardHash);
	while (i.hasNext()) {
		i.next();
		i.value()->clearPixmapCache();
	}
	if (noCard)
		noCard->clearPixmapCache();
}

void CardDatabase::importOracleFile(const QString &fileName, CardSet *set)
{
	QFile file(fileName);
	file.open(QIODevice::ReadOnly | QIODevice::Text);
	QTextStream in(&file);
	while (!in.atEnd()) {
		QString cardname = in.readLine();
		if (cardname.isEmpty())
			continue;
		QString manacost = in.readLine();
		QString cardtype, powtough;
		QStringList text;
		if (manacost.contains("Land", Qt::CaseInsensitive)) {
			cardtype = manacost;
			manacost.clear();
		} else {
			cardtype = in.readLine();
			powtough = in.readLine();
			// Dirty hack.
			// Cards to test: Any creature, any basic land, Ancestral Vision, Fire // Ice.
			if (!powtough.contains("/") || powtough.size() > 5) {
				text << powtough;
				powtough = QString();
			}
		}
		QString line = in.readLine();
		while (!line.isEmpty()) {
			text << line;
			line = in.readLine();
		}
		CardInfo *card;
		if (cardHash.contains(cardname))
			card = cardHash.value(cardname);
		else {
			card = new CardInfo(this, cardname, manacost, cardtype, powtough, text.join("\n"));
			
			int tableRow = 1;
			QString mainCardType = card->getMainCardType();
			if (mainCardType == "Land")
				tableRow = 0;
			else if ((mainCardType == "Sorcery") || (mainCardType == "Instant"))
				tableRow = 2;
			else if (mainCardType == "Creature")
				tableRow = 3;
			card->setTableRow(tableRow);
			
			cardHash.insert(cardname, card);
		}
		card->addToSet(set);
	}
}

void CardDatabase::importOracleDir()
{
	clear();
	QDir dir("../db");

	dir.setSorting(QDir::Name | QDir::IgnoreCase);
	QFileInfoList files = dir.entryInfoList(QStringList() << "*.txt");
	for (int k = 0; k < files.size(); k++) {
		QFileInfo i = files[k];
		QString shortName = i.fileName().left(i.fileName().indexOf('_'));
		QString longName = i.fileName().mid(i.fileName().indexOf('_') + 1);
		longName = longName.left(longName.indexOf('.'));
		CardSet *set = new CardSet(shortName, longName);
		setHash.insert(shortName, set);

		importOracleFile(i.filePath(), set);
	}

	qDebug(QString("CardDatabase: %1 cards imported").arg(cardHash.size()).toLatin1());
}

void CardDatabase::loadSetsFromXml(QXmlStreamReader &xml)
{
	while (!xml.atEnd()) {
		if (xml.readNext() == QXmlStreamReader::EndElement)
			break;
		if (xml.name() == "set") {
			QString shortName, longName;
			while (!xml.atEnd()) {
				if (xml.readNext() == QXmlStreamReader::EndElement)
					break;
				if (xml.name() == "name")
					shortName = xml.readElementText();
				else if (xml.name() == "longname")
					longName = xml.readElementText();
			}
			setHash.insert(shortName, new CardSet(shortName, longName));
		}
	}
}

void CardDatabase::loadCardsFromXml(QXmlStreamReader &xml)
{
	while (!xml.atEnd()) {
		if (xml.readNext() == QXmlStreamReader::EndElement)
			break;
		if (xml.name() == "card") {
			QString name, manacost, type, pt, text;
			SetList sets;
			int tableRow = 0;
			while (!xml.atEnd()) {
				if (xml.readNext() == QXmlStreamReader::EndElement)
					break;
				if (xml.name() == "name")
					name = xml.readElementText();
				else if (xml.name() == "manacost")
					manacost = xml.readElementText();
				else if (xml.name() == "type")
					type = xml.readElementText();
				else if (xml.name() == "pt")
					pt = xml.readElementText();
				else if (xml.name() == "text")
					text = xml.readElementText();
				else if (xml.name() == "set")
					sets << getSet(xml.readElementText());
				else if (xml.name() == "tablerow")
					tableRow = xml.readElementText().toInt();
			}
			cardHash.insert(name, new CardInfo(this, name, manacost, type, pt, text, tableRow, sets));
		}
	}
}

int CardDatabase::loadFromFile(const QString &fileName)
{
	QFile file(fileName);
	file.open(QIODevice::ReadOnly);
	QXmlStreamReader xml(&file);
	clear();
	while (!xml.atEnd()) {
		if (xml.readNext() == QXmlStreamReader::StartElement) {
			if (xml.name() != "cockatrice_carddatabase")
				return false;
			while (!xml.atEnd()) {
				if (xml.readNext() == QXmlStreamReader::EndElement)
					break;
				if (xml.name() == "sets")
					loadSetsFromXml(xml);
				else if (xml.name() == "cards")
					loadCardsFromXml(xml);
			}
		}
	}
	qDebug(QString("%1 cards in %2 sets loaded").arg(cardHash.size()).arg(setHash.size()).toLatin1());
	return cardHash.size();
}

bool CardDatabase::saveToFile(const QString &fileName)
{
	QFile file(fileName);
	file.open(QIODevice::WriteOnly);
	QXmlStreamWriter xml(&file);
	
	xml.setAutoFormatting(true);
	xml.writeStartDocument();
	xml.writeStartElement("cockatrice_carddatabase");
	xml.writeAttribute("version", "1");

	xml.writeStartElement("sets");
	QHashIterator<QString, CardSet *> setIterator(setHash);
	while (setIterator.hasNext())
		xml << setIterator.next().value();
	xml.writeEndElement(); // sets
	
	xml.writeStartElement("cards");
	QHashIterator<QString, CardInfo *> cardIterator(cardHash);
	while (cardIterator.hasNext())
		xml << cardIterator.next().value();
	xml.writeEndElement(); // cards
	
	xml.writeEndElement(); // cockatrice_carddatabase
	xml.writeEndDocument();

	return true;
}

void CardDatabase::updatePicsPath(const QString &path)
{
	if (path.isEmpty()) {
		QSettings settings;
		settings.beginGroup("paths");
		picsPath = settings.value("pics").toString();
	} else
		picsPath = path;
	clearPixmapCache();
}

void CardDatabase::updateDatabasePath(const QString &path)
{
	if (path.isEmpty()) {
		QSettings settings;
		settings.beginGroup("paths");
		cardDatabasePath = settings.value("carddatabase").toString();
	} else
		cardDatabasePath = path;
	loadFromFile(cardDatabasePath);
}
