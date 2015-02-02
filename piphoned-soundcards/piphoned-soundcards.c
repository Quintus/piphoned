#include <stdio.h>
#include <linphone/linphonecore.h>

int main(int argc, char* argv[])
{
  LinphoneCoreVTable vtable = {0};
  LinphoneCore* p_linphone = NULL;
  const char** devices = NULL;
  unsigned int i = 0;

  if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
    printf("This program lists the soundcards piphoned does understand. It accepts no options.");
    return 0;
  }

  p_linphone = linphone_core_new(&vtable, NULL, NULL, NULL);
  devices = linphone_core_get_sound_devices(p_linphone);

  printf("--- List of detected devices ---\n");
  for(i=0; devices[i] != NULL; i++) {
    printf("Device %i: %s\n", i, devices[i]);
  }
  printf("--- End of list of detected devices ---\n");

  linphone_core_destroy(p_linphone);
  return 0;
}
