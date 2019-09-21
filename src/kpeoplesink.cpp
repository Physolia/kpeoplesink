/*************************************************************************************
 *  Copyright (C) 2019 by Rituka Patwal <ritukapatwal21@gmail.com>                   *
 *                                                                                   *
 *  This library is free software; you can redistribute it and/or                    *
 *  modify it under the terms of the GNU Lesser General Public                       *
 *  License as published by the Free Software Foundation; either                     *
 *  version 2.1 of the License, or (at your option) any later version.               *
 *                                                                                   *
 *  This library is distributed in the hope that it will be useful,                  *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of                   *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU                *
 *  Lesser General Public License for more details.                                  *
 *                                                                                   *
 *  You should have received a copy of the GNU Lesser General Public                 *
 *  License along with this library; if not, write to the Free Software              *
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA   *
 *************************************************************************************/
 
#include "kpeoplesink.h"
#include <QDebug>
#include <QImage>
#include <QTimer>
#include <KPeopleBackend/BasePersonsDataSource>
#include <KPeopleBackend/AbstractEditableContact>
#include <KContacts/VCardConverter>
#include <KContacts/Picture>
#include <KPluginFactory>
#include <KPluginLoader>
#include <sink/store.h>
#include <sink/notifier.h>
#include <sink/notification.h>

using namespace KPeople;
using namespace Sink;
using namespace Sink::ApplicationDomain;

class KPeopleSinkDataSource : public KPeople::BasePersonsDataSourceV2
{
public:
    KPeopleSinkDataSource(QObject *parent, const QVariantList &data);
    virtual ~KPeopleSinkDataSource();
    QString sourcePluginId() const override;

    bool addContact(const QVariantMap &properties) {
        //code logic to be added
        return true;
    }
    bool deleteContact(const QString &uri) {
        //code logic to be added
        return true;
    }

    KPeople::AllContactsMonitor* createAllContactsMonitor() override;
};

class SinkContact : public KPeople::AbstractEditableContact
{
public:
    SinkContact() {}
    SinkContact(const Contact & contact)
        : m_contact(contact)
    {
        setContact(contact);
    }

    QVariant customProperty(const QString & key) const override
    {
        QVariant ret;
        if (key == NameProperty) {
            const QString name = m_addressee.realName();
            if (!name.isEmpty()) {
                return name;
            }
            if (!m_addressee.preferredEmail().isEmpty()) {
                return m_addressee.preferredEmail();
            }
            if (!m_addressee.phoneNumbers().isEmpty()) {               
                 // convert from KContacts specific format to QString
                return m_addressee.phoneNumbers().at(0).number();
            }
            return QVariant();
        } 
        else if (key == PictureProperty){
            // convert from KContacts specific format to QString
            return m_addressee.photo().data();
        }
        else if (key == EmailProperty)
            return m_addressee.preferredEmail();

        else if (key == AllPhoneNumbersProperty) {
            QVariantList numbers;
            Q_FOREACH (const KContacts::PhoneNumber &phoneNumber, m_addressee.phoneNumbers()) {
                numbers << phoneNumber.number();
            }
            return numbers;
        } 
        else if (key == PhoneNumberProperty) {
            return m_addressee.phoneNumbers().isEmpty() ? QVariant() : m_addressee.phoneNumbers().at(0).number();
        }
        else if (key == VCardProperty) {
            return m_contact.getVcard();
        }

        return ret;
    }

    void setContact(const Contact & contact) {
        m_contact = contact;
        KContacts::VCardConverter converter;
        m_addressee = converter.parseVCard(contact.getVcard());
    }
    Contact contact() const { return m_contact; }

    bool setCustomProperty(const QString & key, const QVariant & value) override {

        if (key == VCardProperty) {
            const QByteArray rawVCard = value.toByteArray();
            m_contact.setVcard(rawVCard);
            
            KContacts::VCardConverter converter;
            m_addressee = converter.parseVCard(rawVCard);
            //1. fetch resourceId of contact
            QByteArray resourceID = m_contact.resourceInstanceIdentifier();
            //2. call modify function
            Sink::Store::modify<Contact>(Sink::Query().resourceFilter(resourceID), m_contact);
            return true;
        }
        return false;
    }

private:
    Contact m_contact;
    KContacts::Addressee m_addressee;
};

KPeopleSink::KPeopleSink()
    : KPeople::AllContactsMonitor()
{
    qDebug()<<"KPEOPLESINK";
    QTimer::singleShot(500, this, &KPeopleSink::initialSinkContactstoKpeople);
}

void KPeopleSink::initialSinkContactstoKpeople(){
    //fetch all the addressbooks synced by sink
    const QList<Addressbook> sinkAdressbooks = Sink::Store::read<Addressbook>(Sink::Query());
    Q_FOREACH(const Addressbook sinkAddressbook, sinkAdressbooks){
        //to get resourceId
        QByteArray resourceId = sinkAddressbook.resourceInstanceIdentifier();

        //set notifer
        m_notifier = new Notifier(resourceId);
        m_notifier->registerHandler([=] (const Sink::Notification &notification) {
            if (notification.type == Notification::Info && notification.code == SyncStatus::SyncSuccess) {
                processRecentlySyncedContacts(resourceId);
            }
        });

        //fetch all the contacts synced by sink
        const QList<Contact> sinkContacts = Sink::Store::read<Contact>(Sink::Query().resourceFilter(resourceId));
        Q_FOREACH (const Contact sinkContact, sinkContacts){
            //get uri
            const QString uri = getUri(sinkContact, resourceId);

            //add uri of contact to set
            KPeople::AbstractContact::Ptr contact(new SinkContact(sinkContact));
            m_contactUriHash.insert(uri, contact);

            Q_EMIT contactAdded(uri,contact);
        }
    }
}

void KPeopleSink::processRecentlySyncedContacts(QByteArray resourceId){
    const QList<Contact> sinkContacts = Sink::Store::read<Contact>(Sink::Query().resourceFilter(resourceId));
    QSet<QString> contactUri;
    Q_FOREACH (const Contact sinkContact, sinkContacts){
        //get uri
        const QString uri = getUri(sinkContact, resourceId);
        contactUri.insert(uri);

        auto contact = m_contactUriHash.value(uri);
        if(!contact){
            qDebug()<<"ADD CONTACT";
            KPeople::AbstractContact::Ptr contact(new SinkContact(sinkContact));
            m_contactUriHash.insert(uri, contact);
            Q_EMIT contactAdded(uri,contact);
        } else if(static_cast<SinkContact*>(contact.data())->contact().getVcard() != sinkContact.getVcard()){
            qDebug()<<"CHANGE CONTACT"; 
            static_cast<SinkContact*>(contact.data())->setContact(sinkContact);
            Q_EMIT contactChanged(uri,contact);
        }

    }
    toRemoveContact(contactUri);
}

void KPeopleSink::toRemoveContact(QSet<QString> contactUri){
    QHashIterator<QString, AbstractContact::Ptr> i(m_contactUriHash);
    while (i.hasNext()) {
        i.next();
        QString uri = i.key();
        if(!contactUri.contains(uri)){
            qDebug()<<" REMOVE CONTACT";
            m_contactUriHash.remove(uri);
            Q_EMIT contactRemoved(uri);
        }
    }
}

QString KPeopleSink::getUri(Contact sinkContact, QByteArray resourceId)
{
    //to get uid of contact
    QString uid = sinkContact.getUid();
    //to get accountId of contact
    auto resource = Store::readOne<SinkResource>(Sink::Query().filter(resourceId));
    QString accountId = resource.getAccount();
    //create uri for sink contact
    QString uri = "sink://" + accountId + "/" + uid;
    return uri;
}

KPeopleSink::~KPeopleSink()
{}

KPeopleSinkDataSource::KPeopleSinkDataSource(QObject *parent, const QVariantList &args)
: BasePersonsDataSourceV2(parent)
{
    Q_UNUSED(args);
}

KPeopleSinkDataSource::~KPeopleSinkDataSource()
{
}

QString KPeopleSinkDataSource::sourcePluginId() const
{
    return QStringLiteral("sink");
}

AllContactsMonitor* KPeopleSinkDataSource::createAllContactsMonitor()
{
    return new KPeopleSink();
}

K_PLUGIN_FACTORY_WITH_JSON( KPeopleSinkDataSourceFactory, "kpeoplesink.json", registerPlugin<KPeopleSinkDataSource>(); )
K_EXPORT_PLUGIN( KPeopleSinkDataSourceFactory("kpeoplesink") )

#include "kpeoplesink.moc"
