#include "SystemServices.h"

void SystemServices::setAudioPlayer (AudioPlayer* audioPlayer)
{
    setValue (audioPlayer, AudioPlayerPropertyId, false);
}

AudioPlayer* SystemServices::getAudioPlayer ()
{
    return getValue<AudioPlayer*> (AudioPlayerPropertyId, data);
}

void SystemServices::setAudioManager (AudioManager* audioManager)
{
    setValue (audioManager, AudioManagerPropertyId, false);
}

AudioManager* SystemServices::getAudioManager ()
{
    return getValue<AudioManager*> (AudioManagerPropertyId, data);
}

void SystemServices::setEditManager (EditManager* editManager)
{
    setValue (editManager, EditManagerPropertyId, false);
}

EditManager* SystemServices::getEditManager ()
{
    return getValue<EditManager*> (EditManagerPropertyId, data);
}
