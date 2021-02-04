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
#include <QTimer>

#include <sink/notification.h>
#include <sink/store.h>

#include "sinkcontact.h"
#include "sinkdatasource.h"

KPeopleSink::KPeopleSink()
    : KPeople::AllContactsMonitor()
{
    qDebug() << "KPEOPLESINK";
    QTimer::singleShot(500, this, &KPeopleSink::initialSinkContactstoKpeople);
}

void KPeopleSink::initialSinkContactstoKpeople()
{
    // fetch all the addressbooks synced by sink
    const QList<Sink::ApplicationDomain::Addressbook> sinkAdressbooks = Sink::Store::read<Sink::ApplicationDomain::Addressbook>(Sink::Query());
    for (const Sink::ApplicationDomain::Addressbook &sinkAddressbook : sinkAdressbooks) {
        // to get resourceId
        QByteArray resourceId = sinkAddressbook.resourceInstanceIdentifier();

        // fetch all the contacts synced by sink
        const QList<Sink::ApplicationDomain::Contact> sinkContacts = Sink::Store::read<Sink::ApplicationDomain::Contact>(Sink::Query().resourceFilter(resourceId));
        for (const Sink::ApplicationDomain::Contact &sinkContact : sinkContacts) {
            // get uri
            const QString uri = getUri(sinkContact, resourceId);

            auto contact = m_contactUriMap.value(uri);
            if (!contact) {
                // add uri of contact to set
                KPeople::AbstractContact::Ptr contact(new SinkContact(sinkContact));
                m_contactUriMap.insert(uri, contact);

                Q_EMIT contactAdded(uri, contact);
            }
        }

        // set notifer
        m_notifier = new Sink::Notifier(resourceId);
        m_notifier->registerHandler([=](const Sink::Notification &notification) {
            if (notification.type == Sink::Notification::Info && notification.code == Sink::ApplicationDomain::SyncSuccess) {
                processRecentlySyncedContacts(resourceId);
            }
        });
    }
}

void KPeopleSink::processRecentlySyncedContacts(QByteArray resourceId)
{
    const QList<Sink::ApplicationDomain::Contact> sinkContacts = Sink::Store::read<Sink::ApplicationDomain::Contact>(Sink::Query().resourceFilter(resourceId));
    QSet<QString> contactUri;
    for (const Sink::ApplicationDomain::Contact &sinkContact : sinkContacts) {
        // get uri
        const QString uri = getUri(sinkContact, resourceId);
        contactUri.insert(uri);

        auto contact = m_contactUriMap.value(uri);
        if (!contact) {
            qDebug() << "ADD CONTACT";
            KPeople::AbstractContact::Ptr contact(new SinkContact(sinkContact));
            m_contactUriMap.insert(uri, contact);
            Q_EMIT contactAdded(uri, contact);
        } else if (static_cast<SinkContact *>(contact.data())->contact().getVcard() != sinkContact.getVcard()) {
            qDebug() << "CHANGE CONTACT";
            static_cast<SinkContact *>(contact.data())->setAddressee(sinkContact);
            Q_EMIT contactChanged(uri, contact);
        }
    }
    toRemoveContact(contactUri);
}

void KPeopleSink::toRemoveContact(QSet<QString> contactUri)
{
    QMapIterator<QString, KPeople::AbstractContact::Ptr> i(m_contactUriMap);
    while (i.hasNext()) {
        i.next();
        QString uri = i.key();
        if (!contactUri.contains(uri)) {
            qDebug() << " REMOVE CONTACT";
            m_contactUriMap.remove(uri);
            Q_EMIT contactRemoved(uri);
        }
    }
}

QString KPeopleSink::getUri(Sink::ApplicationDomain::Contact sinkContact, QByteArray resourceId)
{
    return QStringLiteral("sink://%1/%2").arg(resourceId, sinkContact.getUid());
}

QMap<QString, KPeople::AbstractContact::Ptr> KPeopleSink::contacts()
{
    return m_contactUriMap;
}
