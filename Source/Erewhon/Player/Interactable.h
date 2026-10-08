#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable.generated.h"

class AErewhonPlayerController;

UINTERFACE(MinimalAPI)
class UErewhonInteractable : public UInterface
{
	GENERATED_BODY()
};

/** Un acteur qu'on vise et qu'on active au clic (marchand, plus tard portes, coffres…). */
class EREWHON_API IErewhonInteractable
{
	GENERATED_BODY()

public:
	/** Texte affiché sous le réticule, ex. « Parler au marchand ». */
	virtual FText GetInteractionLabel() const = 0;

	/** Appelé sur le client qui interagit. */
	virtual void Interact(AErewhonPlayerController* Controller) = 0;
};
