#ifndef VIRTUALMOD_H
#define VIRTUALMOD_H

#include <QObject>

/* Virtual Module is a calculating class that takes a complete event
 * and does any kind of processing (filtering, special histogramming, ...)
 * besides simple channelwise histogramming
*/

class VirtualMod : public QObject
{
    Q_OBJECT
public:
    explicit VirtualMod(QObject *parent = 0);
    
signals:
    
public slots:
    
};

#endif // VIRTUALMOD_H
