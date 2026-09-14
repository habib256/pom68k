/* scsiagent.r — the SIZE resource of « POM68K Disques »: high-level-event
 * aware, because the unmount is a Finder « put away » Apple event
 * (mount.c finderPutAway); AESend answers noPortErr (-903) without it. */
#include "Processes.r"

resource 'SIZE' (-1) {
	reserved,
	acceptSuspendResumeEvents,
	reserved,
	canBackground,
	doesActivateOnFGSwitch,
	backgroundAndForeground,
	dontGetFrontClicks,
	ignoreChildDiedEvents,
	is32BitCompatible,
	isHighLevelEventAware,
	onlyLocalHLEvents,
	notStationeryAware,
	dontUseTextEditServices,
	reserved,
	reserved,
	reserved,
	256 * 1024,
	128 * 1024
};
