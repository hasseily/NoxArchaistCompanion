#pragma once
class NonVolatile
{
public:
	std::wstring profilePath;
	std::wstring hdvPath;
	int volumeSpeaker;
	int volumeMockingBoard;

	// I/O
	int SaveToDisk();
	int LoadFromDisk();
};

