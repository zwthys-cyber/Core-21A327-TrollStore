#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../patch/CoreCosmeticsVehicleEnqueue.h"

int main(void) {
    uint32_t resource = 0;
    assert(CCPEnqueueResourceIDForItem(3349002u, &resource));
    assert(resource == 334900200u);
    assert(CCPEnqueueResourceIDForItem(3349001u, &resource));
    assert(resource == 334900100u);
    assert(!CCPEnqueueResourceIDForItem(0, &resource));
    assert(!CCPEnqueueResourceIDForItem(UINT32_MAX, &resource));
    assert(!CCPEnqueueResourceIDForItem(3349002u, NULL));
    CCPVehicleEnqueueResult special = CCPEnqueuePutOnByResID(
        0, 0, 0, 0, 3312001u, 412009u, 412009u, NULL, NULL, NULL, NULL);
    assert(!strcmp(special.status, "bad-arguments"));
    assert(special.targetItem == 412009u && special.targetResource == 412009u);
    CCPVehicleEnqueueResult missingResource = CCPEnqueuePutOnByResID(
        0, 0, 0, 0, 3312001u, 412009u, 0, NULL, NULL, NULL, NULL);
    assert(!strcmp(missingResource.status, "bad-arguments"));
    CCPVehicleEnqueueResult result = {.status = "target-not-observed", .putOnAttempted = true,
        .targetItem = 3349002u, .skinAfter = 3349001u};
    assert(CCPVehiclePutOnDispatched(&result));
    assert(!CCPVehicleTargetObserved(&result));
    assert(!CCPVehicleRestoreObserved(&result));
    result.status = "target-observed";
    result.skinAfter = result.targetItem;
    assert(CCPVehicleTargetObserved(&result));
    assert(CCPVehicleRestoreObserved(&result));
    result.status = "skipped-already-target";
    result.putOnAttempted = false;
    assert(!CCPVehiclePutOnDispatched(&result));
    assert(CCPVehicleRestoreObserved(&result));
    puts("PASS: vehicle PutOn preserves explicit catalog resource IDs");
    return 0;
}
