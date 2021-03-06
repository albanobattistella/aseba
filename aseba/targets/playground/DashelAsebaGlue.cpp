/*
    Aseba - an event-based framework for distributed robot control
    Copyright (C) 2007--2013:
        Stephane Magnenat <stephane at magnenat dot net>
        (http://stephane.magnenat.net)
        and other contributors, see authors.txt for details

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation, version 3 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "DashelAsebaGlue.h"
#include "EnkiGlue.h"
#include <QDataStream>
#include <QCoreApplication>
#include <QNetworkInterface>
#include "transport/buffer/vm-buffer.h"

namespace Aseba {

SimpleConnectionBase::SimpleConnectionBase(const QString & type, const QString & name, unsigned & port):
    m_server(new QTcpServer(this)), m_client(nullptr), m_zeroconf(new QZeroConf(this)),
    m_robotType(type),
    m_robotName(name) {
    connect(m_server, &QTcpServer::newConnection, this, &SimpleConnectionBase::onNewConnection);
    //Because of an issue with our zeroconf implementation, the TDM might try to connect
    //Using a non-local ip
    m_server->listen(QHostAddress::AnyIPv4, port);
    port = m_server->serverPort();
    m_server->setMaxPendingConnections(1);
    startServiceRegistration();
}

uint16_t SimpleConnectionBase::serverPort() const {
    return m_server->serverPort();
}

SimpleConnectionBase::~SimpleConnectionBase() {
    m_zeroconf->stopServicePublish();
}

void SimpleConnectionBase::onNewConnection() {
    if(m_client) {
        qWarning() << "This Virtual Robot is already connected to a peer";
        SEND_NOTIFICATION(LOG_WARNING, "client already connected", m_robotName.toStdString());
        return;
    }
    auto client = m_server->nextPendingConnection();
    client->open(QIODevice::ReadWrite);
    SEND_NOTIFICATION(LOG_INFO, "client connected", client->peerAddress().toString().toStdString());
    //Reject remote connections
    if(!QNetworkInterface::allAddresses().contains(client->peerAddress())) {
        client->deleteLater();
        return;
    }
    m_client = client;
    m_server->pauseAccepting();
    connect(m_client, &QTcpSocket::disconnected, this, &SimpleConnectionBase::onConnectionClosed);
    connect(m_client, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, &SimpleConnectionBase::onClientError);
}

void SimpleConnectionBase::onClientError() {
    SEND_NOTIFICATION(LOG_ERROR, "client disconnected", m_client->errorString().toStdString());
    onConnectionClosed();
}

void SimpleConnectionBase::onServerError() {
    SEND_NOTIFICATION(LOG_ERROR, "server error", m_server->errorString().toStdString());
}

void SimpleConnectionBase::onConnectionClosed() {
    SEND_NOTIFICATION(LOG_INFO, "client disconnected", m_robotName.toStdString());
    disconnect(m_client);
    m_client->deleteLater();
    m_client = nullptr;
    m_server->resumeAccepting();
    m_messageSize = 0;
    m_lastMessage.clear();
}

void SimpleConnectionBase::startServiceRegistration() {
    m_zeroconf->addServiceTxtRecord("type", m_robotType);
    m_zeroconf->addServiceTxtRecord("protovers", QString::number(9));
    std::string name = QStringLiteral("%1 - %2")
                           .arg(m_robotName)
                           .arg(QCoreApplication::applicationPid()).toStdString();

    m_zeroconf->startServicePublish(
        name.c_str(),
        "_aseba._tcp",
        "local",
        m_server->serverPort(),
        QZeroConf::service_option::localhost_only);
}

void SimpleConnectionBase::sendBuffer(uint16_t nodeId, const uint8_t* data, uint16_t length) {
    if(!m_client)
        return;
    QDataStream stream(m_client);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << uint16_t(length - 2);
    stream << nodeId;
    stream.writeRawData((char*)(data), length);
}

bool SimpleConnectionBase::handleSingleMessageData() {
    if(m_messageSize == 0) {
        if(!m_client || m_client->bytesAvailable() < 2)
            return false;
        QDataStream stream(m_client);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream >> m_messageSize;
    }
    if(!m_lastMessage.isEmpty())
        return false;

    if(m_messageSize != 0 && m_client->bytesAvailable() < m_messageSize + 2)
        return false;

    QDataStream stream(m_client);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream >> m_messageSource;
    QByteArray data(m_messageSize + 2, Qt::Uninitialized);
    stream.readRawData(data.data(), data.size());
    m_lastMessage =  data;
    for(auto vmStateToEnvironmentKV : vmStateToEnvironment) {
        if(vmStateToEnvironmentKV.second.second == this) {
            AsebaProcessIncomingEvents(vmStateToEnvironmentKV.first);
            AsebaVMRun(vmStateToEnvironmentKV.first, 1000);
        }
    }
    return true;
}

uint16_t SimpleConnectionBase::getBuffer(uint8_t* data, uint16_t maxLength, uint16_t* source) {
    *source = m_messageSource;
    uint16_t s = qMin(uint16_t(m_lastMessage.size()), maxLength);
    memcpy(data, m_lastMessage.data(), s);
    m_lastMessage = m_lastMessage.mid(s);
    if(m_lastMessage.isEmpty())
        m_messageSize = 0;
    return s;
}

//! Clear breakpoints on all VM that are linked to this connection
void SimpleConnectionBase::clearBreakpoints() {
    for(auto vmStateToEnvironmentKV : vmStateToEnvironment) {
        if(vmStateToEnvironmentKV.second.second == this)
            vmStateToEnvironmentKV.first->breakpointsCount = 0;
    }
}

}  // namespace Aseba
