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
    puts("PASS: vehicle item IDs convert to PutOn resource IDs without overflow");
    return 0;
}
