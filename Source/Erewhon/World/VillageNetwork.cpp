#include "World/VillageNetwork.h"

#include "World/SectorTypes.h"

namespace ErewhonVillages
{
	namespace
	{
		/** La graine du monde, telle que posée par SetWorldSeed. */
		int32 GVillageWorldSeed = 0;

		/** Division entière arrondie vers le bas, aussi pour les négatifs. */
		int32 VillageFloorDiv(int32 Value, int32 Divisor)
		{
			return Value >= 0 ? Value / Divisor : -((-Value + Divisor - 1) / Divisor);
		}

		/**
		 * La cellule dont le milieu est le plus proche de ce secteur. Un centre ne s'écarte que de
		 * CenterJitter secteurs du milieu : s'il tombe sur ce secteur, c'est celui de cette cellule.
		 */
		FIntPoint VillageCellNear(const FIntPoint& Sector)
		{
			return FIntPoint(VillageFloorDiv(Sector.X + CellSectors / 2, CellSectors), VillageFloorDiv(Sector.Y + CellSectors / 2, CellSectors));
		}

		/** Un tirage par (graine, village, usage), bien mélangé : les villages voisins ne se ressemblent pas. */
		uint32 VillageHash(const FIntPoint& Village, uint32 Salt)
		{
			uint32 Hash = HashCombine(HashCombine(GetTypeHash(GVillageWorldSeed), GetTypeHash(Village)), Salt);
			Hash ^= Hash >> 16;
			Hash *= 0x85EBCA6Bu;
			Hash ^= Hash >> 13;
			Hash *= 0xC2B2AE35u;
			Hash ^= Hash >> 16;
			return Hash;
		}

		/**
		 * Une route entre deux villages voisins, rangée chez celui du sud (Axis = 0, vers le nord) ou
		 * de l'ouest (Axis = 1, vers l'est). Coordonnées « en long » (le long de l'axe) et « en travers ».
		 */
		struct FVillageRoad
		{
			FIntPoint From, To;   // villages
			FIntPoint A, B;       // leurs places
			int32 Axis = 0;
			int32 Middle = 0;     // la rangée (en long) où la route oblique
		};

		int32 RoadLong(const FIntPoint& Point, int32 Axis) { return Axis == 0 ? Point.X : Point.Y; }
		int32 RoadAcross(const FIntPoint& Point, int32 Axis) { return Axis == 0 ? Point.Y : Point.X; }

		FVillageRoad MakeVillageRoad(const FIntPoint& From, int32 Axis)
		{
			FVillageRoad Road;
			Road.From = From;
			Road.To = From + (Axis == 0 ? FIntPoint(1, 0) : FIntPoint(0, 1));
			Road.A = CenterOf(Road.From);
			Road.B = CenterOf(Road.To);
			Road.Axis = Axis;
			Road.Middle = VillageFloorDiv(RoadLong(Road.A, Axis) + RoadLong(Road.B, Axis), 2);
			return Road;
		}

		/**
		 * Secteur du tracé en Z, places exclues : droit devant depuis A jusqu'à mi-chemin, de travers
		 * jusqu'à la colonne de B, puis droit jusqu'à B.
		 */
		bool IsOnVillageRoad(const FVillageRoad& Road, const FIntPoint& Sector)
		{
			const int32 L = RoadLong(Sector, Road.Axis);
			const int32 T = RoadAcross(Sector, Road.Axis);
			const int32 AL = RoadLong(Road.A, Road.Axis);
			const int32 AT = RoadAcross(Road.A, Road.Axis);
			const int32 BL = RoadLong(Road.B, Road.Axis);
			const int32 BT = RoadAcross(Road.B, Road.Axis);
			return (T == AT && L > AL && L <= Road.Middle)
				|| (L == Road.Middle && T >= FMath::Min(AT, BT) && T <= FMath::Max(AT, BT))
				|| (T == BT && L >= Road.Middle && L < BL);
		}

		/**
		 * La route qui passe par ce secteur. Toute route vers le nord reste à CenterJitter secteurs
		 * (en travers) du milieu de sa cellule, toute route vers l'est aussi : hors de ces bandes, pas
		 * de route ; dedans, seules les routes des cellules voisines sont à essayer.
		 */
		bool FindVillageRoad(const FIntPoint& Sector, FVillageRoad& OutRoad)
		{
			const FIntPoint Cell = VillageCellNear(Sector);
			const int32 OffX = Sector.X - Cell.X * CellSectors;
			const int32 OffY = Sector.Y - Cell.Y * CellSectors;
			if (FMath::Abs(OffY) <= CenterJitter)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					const FVillageRoad Road = MakeVillageRoad(FIntPoint(Cell.X + DX, Cell.Y), 0);
					if (IsOnVillageRoad(Road, Sector))
					{
						OutRoad = Road;
						return true;
					}
				}
			}
			if (FMath::Abs(OffX) <= CenterJitter)
			{
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					const FVillageRoad Road = MakeVillageRoad(FIntPoint(Cell.X, Cell.Y + DY), 1);
					if (IsOnVillageRoad(Road, Sector))
					{
						OutRoad = Road;
						return true;
					}
				}
			}
			return false;
		}

		/** Côtés par lesquels la route continue : vers un secteur de son tracé, ou l'une de ses deux places. */
		uint8 VillageRoadSides(const FVillageRoad& Road, const FIntPoint& Sector)
		{
			uint8 Bits = 0;
			for (int32 Side = 0; Side < 4; ++Side)
			{
				const FIntPoint Next = Sector + ErewhonSector::Sides[Side];
				if (Next == Road.A || Next == Road.B || IsOnVillageRoad(Road, Next))
				{
					Bits |= 1 << Side;
				}
			}
			return Bits;
		}

		/** Une place, ou un secteur qui la touche (le premier de chaque route qui en sort). */
		bool IsBesidePlaza(const FIntPoint& Sector)
		{
			if (IsCenter(Sector))
			{
				return true;
			}
			for (const FIntPoint& Step : ErewhonSector::Sides)
			{
				if (IsCenter(Sector + Step))
				{
					return true;
				}
			}
			return false;
		}

		// ── Noms ──

		/** Racines douces : la nature, les saisons, les petites bêtes. */
		const TCHAR* const GVillageRoots[] = {
			TEXT("Hana"), TEXT("Kaze"), TEXT("Tsuki"), TEXT("Hoshi"), TEXT("Mizu"), TEXT("Kiri"), TEXT("Yuki"), TEXT("Sora"),
			TEXT("Hotaru"), TEXT("Kumo"), TEXT("Haru"), TEXT("Natsu"), TEXT("Aki"), TEXT("Momo"), TEXT("Sakura"), TEXT("Ume"),
			TEXT("Kaede"), TEXT("Suzu"), TEXT("Akane"), TEXT("Midori"), TEXT("Aoi"), TEXT("Shizu"), TEXT("Nagi"), TEXT("Hikari"),
			TEXT("Asa"), TEXT("Niji"), TEXT("Tsubaki"), TEXT("Hinata"), TEXT("Kogane"), TEXT("Sasa"), TEXT("Kitsune"), TEXT("Tanuki"),
			TEXT("Fuku"), TEXT("Koto"), TEXT("Ruri"), TEXT("Sumire"), TEXT("Tama"), TEXT("Yomogi"), TEXT("Nono"), TEXT("Usagi"),
			TEXT("Kawa"), TEXT("Iwa"), TEXT("Hibari"), TEXT("Komugi"), TEXT("Yuzu"), TEXT("Kiku"), TEXT("Wakaba"), TEXT("Matsu"),
		};

		/** Accolés à la racine : bois, village, plaine, marais, vallée, mont, cap, pont. */
		const TCHAR* const GVillageSuffixes[] = {
			TEXT("mori"), TEXT("mura"), TEXT("hara"), TEXT("zawa"), TEXT("dani"), TEXT("yama"), TEXT("saki"), TEXT("bashi"),
		};

		/** Après « -no- » : la colline, le hameau, le bois, la plaine, la vallée, la source. */
		const TCHAR* const GVillagePlaces[] = {
			TEXT("Oka"), TEXT("Sato"), TEXT("Mori"), TEXT("Hara"), TEXT("Tani"), TEXT("Izumi"),
		};

		/** Des mots entiers, comme des noms de lieux-dits. */
		const TCHAR* const GVillageWords[] = {
			TEXT("Komorebi"), TEXT("Hidamari"), TEXT("Kazahana"), TEXT("Yuugure"), TEXT("Hotarubi"), TEXT("Asagiri"),
			TEXT("Harukaze"), TEXT("Sazanami"), TEXT("Kogarashi"), TEXT("Akatsuki"), TEXT("Shinonome"), TEXT("Hanabira"),
			TEXT("Yamabiko"), TEXT("Natsukusa"), TEXT("Tsukiakari"), TEXT("Kawasemi"),
		};

		constexpr uint32 GVillageRootCount = UE_ARRAY_COUNT(GVillageRoots);

		/** Accord racine + suffixe : ni répétition (« Morimori »), ni bégaiement (« Kawazawa », « Yuzuzawa »). */
		bool IsGentleCompound(const FString& Root, const FString& Suffix)
		{
			const FString Lower = Root.ToLower();
			if (Lower.Contains(Suffix.Left(2)) || Lower.EndsWith(Suffix.Right(2)))
			{
				return false;
			}
			// La consonne de la dernière syllabe de la racine contre celle du suffixe.
			return Lower.Len() < 2 || Lower[Lower.Len() - 2] != Suffix[0];
		}

		/** Un nom candidat pour ce village (tentative Attempt) ; OutKey : son mot principal, pour éviter les doublons. */
		FString ComposeVillageName(const FIntPoint& Village, int32 Attempt, int32& OutKey)
		{
			for (int32 Try = 0; ; ++Try)
			{
				const uint32 Hash = VillageHash(Village, 0x4A3E0000u + static_cast<uint32>(Attempt * 16 + Try));
				const uint32 Pattern = Hash % 100;
				if (Try >= 8 || Pattern < 15)
				{
					const int32 Word = static_cast<int32>((Hash / 100) % UE_ARRAY_COUNT(GVillageWords));
					OutKey = static_cast<int32>(GVillageRootCount) + Word;
					return GVillageWords[Word];
				}
				const int32 RootIndex = static_cast<int32>((Hash / 100) % GVillageRootCount);
				const FString Root = GVillageRoots[RootIndex];
				OutKey = RootIndex;
				if (Pattern < 40)
				{
					// « Kazeno-Oka » : le … de la colline.
					const FString Place = GVillagePlaces[(Hash / 10000) % UE_ARRAY_COUNT(GVillagePlaces)];
					// Pas de « Nonono- », ni de « Morino-Mori ».
					if (Root.Len() <= 6 && !Root.EndsWith(TEXT("no")) && !Root.ToLower().Contains(Place.ToLower()))
					{
						return Root + TEXT("no-") + Place;
					}
					continue;
				}
				const FString Suffix = GVillageSuffixes[(Hash / 10000) % UE_ARRAY_COUNT(GVillageSuffixes)];
				if (IsGentleCompound(Root, Suffix))
				{
					return Root + Suffix;
				}
			}
		}
	}

	void SetWorldSeed(int32 Seed)
	{
		GVillageWorldSeed = Seed;
	}

	int32 GetWorldSeed()
	{
		return GVillageWorldSeed;
	}

	FIntPoint CenterOf(const FIntPoint& Village)
	{
		if (Village == StartVillage)
		{
			return FIntPoint::ZeroValue;
		}
		const uint32 Hash = VillageHash(Village, 0xCE47E5u);
		const uint32 Span = CenterJitter * 2 + 1;
		const FIntPoint Jitter(static_cast<int32>(Hash % Span) - CenterJitter, static_cast<int32>((Hash / Span) % Span) - CenterJitter);
		return FIntPoint(Village.X * CellSectors, Village.Y * CellSectors) + Jitter;
	}

	bool IsCenter(const FIntPoint& Sector, FIntPoint* OutVillage)
	{
		const FIntPoint Village = VillageCellNear(Sector);
		if (CenterOf(Village) != Sector)
		{
			return false;
		}
		if (OutVillage)
		{
			*OutVillage = Village;
		}
		return true;
	}

	FIntPoint NearestVillage(const FIntPoint& Sector)
	{
		// Le plus proche est forcément dans l'une des cellules voisines de celle du secteur.
		const FIntPoint Cell = VillageCellNear(Sector);
		FIntPoint Best = Cell;
		int64 BestDistance = MAX_int64;
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				const FIntPoint Village = Cell + FIntPoint(DX, DY);
				const FIntPoint Delta = CenterOf(Village) - Sector;
				const int64 Distance = static_cast<int64>(Delta.X) * Delta.X + static_cast<int64>(Delta.Y) * Delta.Y;
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = Village;
				}
			}
		}
		return Best;
	}

	void VillagesIn(const FIntPoint& MinSector, const FIntPoint& MaxSector, TArray<FIntPoint>& OutVillages)
	{
		OutVillages.Reset();
		// Les cellules dont le centre peut tomber dans le rectangle (à CenterJitter près).
		const int32 MinX = -VillageFloorDiv(-(MinSector.X - CenterJitter), CellSectors);
		const int32 MinY = -VillageFloorDiv(-(MinSector.Y - CenterJitter), CellSectors);
		const int32 MaxX = VillageFloorDiv(MaxSector.X + CenterJitter, CellSectors);
		const int32 MaxY = VillageFloorDiv(MaxSector.Y + CenterJitter, CellSectors);
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				const FIntPoint Center = CenterOf(FIntPoint(X, Y));
				if (Center.X >= MinSector.X && Center.X <= MaxSector.X && Center.Y >= MinSector.Y && Center.Y <= MaxSector.Y)
				{
					OutVillages.Add(FIntPoint(X, Y));
				}
			}
		}
	}

	FText VillageName(const FIntPoint& Village)
	{
		// Pas deux voisins sur le même mot : on repioche tant qu'un village d'à côté (son premier
		// tirage) l'a déjà.
		int32 NeighbourKeys[8] = {};
		int32 Count = 0;
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				if (DX != 0 || DY != 0)
				{
					ComposeVillageName(Village + FIntPoint(DX, DY), 0, NeighbourKeys[Count++]);
				}
			}
		}
		int32 Key = 0;
		const FString First = ComposeVillageName(Village, 0, Key);
		for (int32 Attempt = 0; Attempt < 6; ++Attempt)
		{
			const FString Candidate = Attempt == 0 ? First : ComposeVillageName(Village, Attempt, Key);
			bool bClash = false;
			for (const int32 Other : NeighbourKeys)
			{
				bClash |= Other == Key;
			}
			if (!bClash)
			{
				return FText::AsCultureInvariant(Candidate);
			}
		}
		return FText::AsCultureInvariant(First);
	}

	FTransform VillageFrame(const FIntPoint& Village)
	{
		const int32 Quarters = Village == StartVillage ? 0 : static_cast<int32>(VillageHash(Village, 0x0F3A7Eu) % 4);
		return FTransform(FRotator(0.f, 90.f * Quarters, 0.f), ErewhonSector::Center(CenterOf(Village)));
	}

	bool IsRoad(const FIntPoint& Sector)
	{
		FVillageRoad Road;
		return FindVillageRoad(Sector, Road);
	}

	uint8 RoadSides(const FIntPoint& Sector)
	{
		if (IsCenter(Sector))
		{
			return 0xF;
		}
		FVillageRoad Road;
		return FindVillageRoad(Sector, Road) ? VillageRoadSides(Road, Sector) : 0;
	}

	bool RoadDestination(const FIntPoint& Sector, int32 Side, FIntPoint& OutVillage)
	{
		if (Side < 0 || Side >= 4)
		{
			return false;
		}
		FIntPoint Village;
		if (IsCenter(Sector, &Village))
		{
			FIntPoint Around[4];
			Neighbours(Village, Around);
			OutVillage = Around[Side];
			return true;
		}
		FVillageRoad Road;
		if (!FindVillageRoad(Sector, Road) || !(VillageRoadSides(Road, Sector) & (1 << Side)))
		{
			return false;
		}
		// Le tracé ne revient jamais en arrière : en long vers l'avant, ou de travers du côté de B,
		// on va vers B ; sinon vers A.
		const FIntPoint Step = ErewhonSector::Sides[Side];
		const int32 StepLong = RoadLong(Step, Road.Axis);
		const int32 Shift = RoadAcross(Road.B, Road.Axis) - RoadAcross(Road.A, Road.Axis);
		const bool bTowardB = StepLong != 0 ? StepLong > 0 : FMath::Sign(RoadAcross(Step, Road.Axis)) == FMath::Sign(Shift);
		OutVillage = bTowardB ? Road.To : Road.From;
		return true;
	}

	void Neighbours(const FIntPoint& Village, FIntPoint OutNeighbours[4])
	{
		for (int32 Side = 0; Side < 4; ++Side)
		{
			OutNeighbours[Side] = Village + ErewhonSector::Sides[Side];
		}
	}

	float RoadCrossing(const FIntPoint& Sector, int32 Side)
	{
		if (Side < 0 || Side >= 4)
		{
			return 0.f;
		}
		// Près des villages, droit : les maisons, panneaux et lanternes y sont posés le long de l'axe.
		const FIntPoint Other = Sector + ErewhonSector::Sides[Side];
		if (IsBesidePlaza(Sector) || IsBesidePlaza(Other))
		{
			return 0.f;
		}
		// Le côté partagé, désigné par le secteur du sud (ou de l'ouest) et son axe : ses deux secteurs le voient pareil.
		const int32 Axis = Side < 2 ? 0 : 1;
		const FIntPoint Low = (Side & 1) ? Other : Sector;
		const FVector2D Middle = FVector2D(Low) + (Axis == 0 ? FVector2D(1.0, 0.5) : FVector2D(0.5, 1.0));
		// Une houle à l'échelle de quelques secteurs, décalée pour chaque monde : de longs méandres plutôt
		// qu'un zigzag, et un peu de hasard propre au côté pour que deux secteurs ne se ressemblent pas.
		const uint32 Phase = VillageHash(FIntPoint::ZeroValue, 0x5E1D5EEDu);
		const FVector2D Shift((Phase & 0xFFFF) / 65536.0 * 512.0 + 0.37, (Phase >> 16) / 65536.0 * 512.0 + 0.61);
		const float Swell = FMath::PerlinNoise2D(Middle * 0.31 + Shift) * 1.6f;
		const float Jitter = (VillageHash(Low, 0x5E1D0000u + Axis) & 0xFFFF) / 65535.f * 2.f - 1.f;
		return RoadSway * FMath::Clamp(Swell * 0.8f + Jitter * 0.35f, -1.f, 1.f);
	}

	bool RoadCurve(const FIntPoint& Sector, FVector2D OutControls[4])
	{
		FVillageRoad Road;
		if (IsCenter(Sector) || !FindVillageRoad(Sector, Road))
		{
			return false;
		}
		// Les deux côtés où la route continue : son passage, et la direction vers l'extérieur.
		const uint8 Exits = VillageRoadSides(Road, Sector);
		FVector2D Ends[2];
		FVector2D Outward[2];
		int32 Count = 0;
		for (int32 Side = 0; Side < 4 && Count < 2; ++Side)
		{
			if (!(Exits & (1 << Side)))
			{
				continue;
			}
			const FVector2D Out(ErewhonSector::Sides[Side]);
			const FVector2D Along(FMath::Abs(Out.Y), FMath::Abs(Out.X));
			Ends[Count] = FVector2D(0.5, 0.5) + Out * 0.5 + Along * RoadCrossing(Sector, Side);
			Outward[Count] = Out;
			++Count;
		}
		if (Count < 2)
		{
			return false;
		}
		// Tout droit : un S doux, les poignées aux deux cinquièmes du secteur. Un coude : un quart
		// d'ellipse, poignées à 0,55 de la distance au coin des deux tangentes.
		double Lead = 0.4, Trail = 0.4;
		const FVector2D Delta = Ends[1] - Ends[0];
		if (Outward[0] != -Outward[1])
		{
			Lead = 0.5523 * FMath::Abs(FVector2D::DotProduct(Delta, -Outward[0]));
			Trail = 0.5523 * FMath::Abs(FVector2D::DotProduct(Delta, Outward[1]));
		}
		OutControls[0] = Ends[0];
		OutControls[1] = Ends[0] - Outward[0] * Lead;
		OutControls[2] = Ends[1] - Outward[1] * Trail;
		OutControls[3] = Ends[1];
		return true;
	}
}
