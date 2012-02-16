// -*- Mode: c++ -*-

// Qt headers
#include <QUdpSocket>

// MythTV headers
#include "iptvstreamhandler.h"
#include "rtppacketbuffer.h"
#include "rtptsdatapacket.h"
#include "rtpdatapacket.h"
#include "rtpfecpacket.h"
#include "mythlogging.h"

#define LOC QString("IPTVSH(%1): ").arg(_device)

QMap<QString,IPTVStreamHandler*> IPTVStreamHandler::s_handlers;
QMap<QString,uint>               IPTVStreamHandler::s_handlers_refcnt;
QMutex                           IPTVStreamHandler::s_handlers_lock;

IPTVStreamHandler *IPTVStreamHandler::Get(const QString &devname)
{
    QMutexLocker locker(&s_handlers_lock);

    QString devkey = devname.toUpper();

    QMap<QString,IPTVStreamHandler*>::iterator it = s_handlers.find(devkey);

    if (it == s_handlers.end())
    {
        IPTVStreamHandler *newhandler = new IPTVStreamHandler(devkey);
        newhandler->Open();
        s_handlers[devkey] = newhandler;
        s_handlers_refcnt[devkey] = 1;

        LOG(VB_RECORD, LOG_INFO,
            QString("IPTVSH: Creating new stream handler %1 for %2")
                .arg(devkey).arg(devname));
    }
    else
    {
        s_handlers_refcnt[devkey]++;
        uint rcount = s_handlers_refcnt[devkey];
        LOG(VB_RECORD, LOG_INFO,
            QString("IPTVSH: Using existing stream handler %1 for %2")
                .arg(devkey)
                .arg(devname) + QString(" (%1 in use)").arg(rcount));
    }

    return s_handlers[devkey];
}

void IPTVStreamHandler::Return(IPTVStreamHandler * & ref)
{
    QMutexLocker locker(&s_handlers_lock);

    QString devname = ref->_device;

    QMap<QString,uint>::iterator rit = s_handlers_refcnt.find(devname);
    if (rit == s_handlers_refcnt.end())
        return;

    if (*rit > 1)
    {
        ref = NULL;
        (*rit)--;
        return;
    }

    QMap<QString,IPTVStreamHandler*>::iterator it = s_handlers.find(devname);
    if ((it != s_handlers.end()) && (*it == ref))
    {
        LOG(VB_RECORD, LOG_INFO, QString("IPTVSH: Closing handler for %1")
                           .arg(devname));
        ref->Close();
        delete *it;
        s_handlers.erase(it);
    }
    else
    {
        LOG(VB_GENERAL, LOG_ERR,
            QString("IPTVSH Error: Couldn't find handler for %1")
                .arg(devname));
    }

    s_handlers_refcnt.erase(rit);
    ref = NULL;
}

IPTVStreamHandler::IPTVStreamHandler(const QString &device) :
    StreamHandler(device)
{
    QStringList parts = device.split("!");
    if (parts.size() >= 5)
    {
        m_addr = QHostAddress(parts[0]);
        m_ports[0] = parts[1].toInt();
        m_ports[1] = parts[2].toInt();
        m_ports[2] = parts[3].toInt();
        m_bitrate = parts[4].toInt();
    }
    else
    {
        m_ports[0] = -1;
        m_ports[1] = -1;
        m_ports[2] = -1;
        m_bitrate = -1;
    }
}

void IPTVStreamHandler::run(void)
{
    RunProlog();

    // TODO Error handling..

    // Setup
    for (uint i = 0; i < sizeof(m_ports)/sizeof(int); i++)
    {
        if (m_ports[i] >= 0)
        {
            m_sockets[i] = new QUdpSocket();
            m_read_helpers[i] = new IPTVStreamHandlerReadHelper(this, m_sockets[i], i);
            m_sockets[i]->bind(m_addr, m_ports[i]);
        }
    }
    m_buffer = new RTPPacketBuffer(m_bitrate);
    m_write_helper = new IPTVStreamHandlerWriteHelper(this);

    // Enter event loop
    exec();

    // Clean up
    for (uint i = 0; i < sizeof(m_ports)/sizeof(int); i++)
    {
        if (m_sockets[i])
        {
            delete m_sockets[i];
            m_sockets[i] = NULL;
            delete m_read_helpers[i];
            m_read_helpers[i] = NULL;
        }
    }
    delete m_buffer;
    m_buffer = NULL;
    delete m_write_helper;
    m_write_helper = NULL;

    RunEpilog();
}

void IPTVStreamHandlerReadHelper::ReadPending(void)
{
    QHostAddress sender;
    quint16 senderPort;

    if (0 == m_stream)
    {
        while (m_socket->hasPendingDatagrams())
        {
            RTPDataPacket packet(m_parent->m_buffer->GetEmptyPacket());
            QByteArray &data = packet.GetDataReference();
            data.resize(m_socket->pendingDatagramSize());
            m_socket->readDatagram(data.data(), data.size(),
                                   &sender, &senderPort);
            m_parent->m_buffer->PushDataPacket(packet);
        }
    }
    else
    {
        while (m_socket->hasPendingDatagrams())
        {
            RTPFECPacket packet(m_parent->m_buffer->GetEmptyPacket());
            QByteArray &data = packet.GetDataReference();
            data.resize(m_socket->pendingDatagramSize());
            m_socket->readDatagram(data.data(), data.size(),
                                   &sender, &senderPort);
            m_parent->m_buffer->PushFECPacket(packet, m_stream - 1);
        }
    }
}

#define LOC_WH QString("IPTVSH(%1): ").arg(m_parent->_device)

void IPTVStreamHandlerWriteHelper::timerEvent(QTimerEvent*)
{
    if (!m_parent->m_buffer->HasAvailablePacket())
        return;

    while (true)
    {
        RTPDataPacket packet(m_parent->m_buffer->PopDataPacket());

        if (!packet.IsValid())
            break;

        if (packet.GetPayloadType() == RTPDataPacket::kPayLoadTypeTS)
        {
            RTPTSDataPacket ts_packet(packet);

            if (!ts_packet.IsValid())
            {
                m_parent->m_buffer->FreePacket(packet);
                continue;
            }

            m_parent->_listener_lock.lock();

            int remainder = 0;
            IPTVStreamHandler::StreamDataList::const_iterator sit;
            sit = m_parent->_stream_data_list.begin();
            for (; sit != m_parent->_stream_data_list.end(); ++sit)
                remainder = sit.key()->ProcessData(ts_packet.GetTSData(), ts_packet.GetTSDataSize());

            m_parent->_listener_lock.unlock();

            if (remainder != 0)
            {
                LOG(VB_RECORD, LOG_INFO, LOC_WH +
                    QString("RunTS(): data_length = %1 remainder = %2")
                    .arg(ts_packet.GetTSDataSize()).arg(remainder));
            }
        }

        m_parent->m_buffer->FreePacket(packet);
    }
}
