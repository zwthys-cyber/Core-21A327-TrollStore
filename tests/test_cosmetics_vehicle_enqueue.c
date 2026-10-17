#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../patch/CoreCosmeticsVehicleEnqueue.h"

int main(void) {
    uint32_t resource = 0;
    assert(CCPStandardVehicleResourceIDForItem(3349002u, &resource));
    assert(resource == 334900200u);
    assert(CCPStandardVehicleResourceIDForItem(3349001u, &resource));
    assert(resource == 334900100u);
    assert(!CCPStandardVehicleResourceIDForItem(0, &resource));
    assert(!CCPStandardVehicleResourceIDForItem(412009u, &resource));
    assert(!CCPStandardVehicleResourceIDForItem(UINT32_MAX, &resource));
    assert(!CCPStandardVehicleResourceIDForItem(3349002u, NULL));
    CCPVehicleEnqueueResult special = CCPEnqueuePutOnByResID(
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3312001u, 0,
        412009u, 412009u, false, NULL, NULL, NULL);
    assert(!strcmp(special.status, "bad-arguments"));
    assert(special.targetItem == 412009u && special.targetResource == 412009u);
    CCPVehicleEnqueueResult missingResource = CCPEnqueuePutOnByResID(
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3312001u, 0,
        412009u, 0, false, NULL, NULL, NULL);
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
    result.taskCompleted = true;
    assert(!CCPVehiclePutOnDispatched(&result));
    assert(CCPVehicleRestoreObserved(&result));
    result.status = "task-not-completed";
    result.putOnAttempted = true;
    assert(CCPVehiclePutOnDispatched(&result));
    result.status = "create-and-dispatch-call-failed";
    assert(CCPVehiclePutOnDispatched(&result));
    result.status = "task-context-changed";
    result.taskRejectedContext = true;
    result.mappingReleased = false;
    assert(CCPVehiclePutOnDispatched(&result));
    result.mappingReleased = true;
    assert(!CCPVehiclePutOnDispatched(&result));
    puts("PASS: vehicle PutOn preserves explicit catalog resource IDs");
    return 0;
}
