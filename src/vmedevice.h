#ifndef VMEDEVICE_H
#define VMEDEVICE_H

#include <QObject>
#include <QString>

class mvme;

class VmeDevice : public QObject
{
    Q_OBJECT
public:
    explicit VmeDevice(QObject *parent = 0);

    void setModId(quint16 id);
    void setModRes(quint16 res);
    void setModAddress(quint32 addr);
    void setModName(QString name);
    
signals:
    
public slots:

private:
    quint32 m_baseAddress;
    quint16 m_moduleId;
    quint16 m_resolution;

    QString m_deviceName;

    mvme* m_myMvme;

;


};

#endif // VMEDEVICE_H
