package ui

type containerConfig struct {
	Endpoints struct {
		WidthRatio  float64
		HeightRatio float64
		Margin      int
	}
	Data struct {
		WidthRatio float64
		Height     int
		Margin     int
	}
	StatusBar struct {
		WidthRatio float64
		Height     int
		Margin     int
	}
}

type containerSizes struct {
	Endpoints containerSize
	Data      containerSize
	StatusBar containerSize
}

type containerSize struct {
	Width  int
	Height int
}

var defaultContainerConfig = containerConfig{
	Endpoints: struct {
		WidthRatio  float64
		HeightRatio float64
		Margin      int
	}{
		WidthRatio:  0.3,
		HeightRatio: 1.0,
		Margin:      2,
	},
	Data: struct {
		WidthRatio float64
		Height     int
		Margin     int
	}{
		WidthRatio: 0.7,
		Height:     0,
		Margin:     2,
	},
	StatusBar: struct {
		WidthRatio float64
		Height     int
		Margin     int
	}{
		WidthRatio: 1.0,
		Height:     1,
		Margin:     0,
	},
}

func calculateContainerSizes(windowWidth, windowHeight int) containerSizes {
	config := defaultContainerConfig
	sizes := containerSizes{}

	if windowWidth < 20 {
		windowWidth = 20
	}
	if windowHeight < 10 {
		windowHeight = 10
	}

	topBarHeight := 1
	if windowHeight > topBarHeight {
		windowHeight -= topBarHeight
	}

	endpointsWidth := int(float64(windowWidth)*config.Endpoints.WidthRatio) - config.Endpoints.Margin
	endpointsHeight := windowHeight - config.StatusBar.Height - config.StatusBar.Margin - config.Endpoints.Margin - 2
	if endpointsWidth < 10 {
		endpointsWidth = 10
	}
	if endpointsHeight < 3 {
		endpointsHeight = 3
	}
	sizes.Endpoints = containerSize{
		Width:  endpointsWidth,
		Height: endpointsHeight,
	}

	dataWidth := int(float64(windowWidth)*config.Data.WidthRatio) - config.Data.Margin
	dataHeight := windowHeight - config.StatusBar.Height - config.StatusBar.Margin - config.Data.Margin - 2
	if dataWidth < 10 {
		dataWidth = 10
	}
	if dataHeight < 5 {
		dataHeight = 5
	}
	sizes.Data = containerSize{
		Width:  dataWidth,
		Height: dataHeight,
	}

	statusBarWidth := int(float64(windowWidth)*config.StatusBar.WidthRatio) - config.StatusBar.Margin
	statusBarHeight := config.StatusBar.Height
	if statusBarWidth < 10 {
		statusBarWidth = 10
	}
	if statusBarHeight < 1 {
		statusBarHeight = 1
	}
	sizes.StatusBar = containerSize{
		Width:  statusBarWidth,
		Height: statusBarHeight,
	}

	return sizes
}
